#!/usr/bin/env python3
"""Minimal LoRA trainer for a SD1.5 checkpoint, tailored to this project.

Why hand-rolled: kohya/sd-scripts would not install in this environment (the git
install hung), but diffusers + peft did. This is the smallest loop that actually
produces a usable LoRA:

  * load the SD1.5 single-file checkpoint
  * freeze VAE and text encoder, wrap only the UNet with a LoRA adapter
  * train on <dataset>/*.png with the matching .txt captions
  * save the adapter as safetensors

Usage:
  train_lora.py <ckpt.safetensors> <dataset_dir> <out.safetensors>
                [--steps 800] [--rank 4] [--lr 1e-4] [--res 512]
"""
import argparse
import glob
import os
import random
import sys

import torch
import torch.nn.functional as F
from PIL import Image
from torch.utils.data import Dataset, DataLoader

from diffusers import AutoencoderKL, DDPMScheduler, StableDiffusionPipeline, UNet2DConditionModel
from peft import LoraConfig, get_peft_model
from transformers import CLIPTextModel, CLIPTokenizer


class SpriteDataset(Dataset):
    def __init__(self, folder, tokenizer, res=512):
        self.items = []
        for p in sorted(glob.glob(os.path.join(folder, "*.png"))):
            txt = os.path.splitext(p)[0] + ".txt"
            if not os.path.exists(txt):
                continue
            with open(txt, "r", encoding="utf-8") as f:
                cap = f.read().strip()
            self.items.append((p, cap))
        self.tok = tokenizer
        self.res = res
        print(f"[data] {len(self.items)} samples")

    def __len__(self):
        return len(self.items)

    def __getitem__(self, i):
        path, cap = self.items[i]
        im = Image.open(path).convert("RGB")
        # centre crop then resize; the dataset is already square 512x512
        w, h = im.size
        s = min(w, h)
        im = im.crop(((w - s) // 2, (h - s) // 2, (w + s) // 2, (h + s) // 2))
        im = im.resize((self.res, self.res), Image.LANCZOS)
        px = torch.from_numpy(
            __import__("numpy").array(im, dtype="float32")
        ).permute(2, 0, 1) / 127.5 - 1.0
        toks = self.tok(
            cap,
            padding="max_length",
            truncation=True,
            max_length=self.tok.model_max_length,
            return_tensors="pt",
        )
        return {"pixel_values": px, "input_ids": toks.input_ids[0], "attention_mask": toks.attention_mask[0]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ckpt")
    ap.add_argument("dataset")
    ap.add_argument("out")
    ap.add_argument("--steps", type=int, default=800)
    ap.add_argument("--rank", type=int, default=4)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--res", type=int, default=512)
    ap.add_argument("--bs", type=int, default=1)
    a = ap.parse_args()

    device = "cuda" if torch.cuda.is_available() else "cpu"
    dtype = torch.float16 if device == "cuda" else torch.float32
    print(f"[env] device={device} dtype={dtype}")

    # Load components straight from the single-file checkpoint
    pipe = StableDiffusionPipeline.from_single_file(a.ckpt, torch_dtype=dtype)
    vae = pipe.vae.to(device, dtype=torch.float32)
    text_encoder = pipe.text_encoder.to(device, dtype=dtype)
    tokenizer = pipe.tokenizer
    unet = pipe.unet.to(device, dtype=dtype)
    noise_scheduler = DDPMScheduler.from_config(pipe.scheduler.config)

    vae.requires_grad_(False)
    text_encoder.requires_grad_(False)
    unet.requires_grad_(False)

    # LoRA on the attention projections only - that is where style lives, and it keeps
    # the adapter small enough that 18 training images will not memorise the set.
    lora_cfg = LoraConfig(
        r=a.rank,
        lora_alpha=a.rank,
        target_modules=["to_q", "to_k", "to_v", "to_out.0"],
        lora_dropout=0.0,
        bias="none",
    )
    unet = get_peft_model(unet, lora_cfg)
    unet.print_trainable_parameters()
    unet.train()

    ds = SpriteDataset(a.dataset, tokenizer, a.res)
    if len(ds) == 0:
        print("[error] empty dataset")
        return 1
    dl = DataLoader(ds, batch_size=a.bs, shuffle=True, drop_last=True)

    params = [p for p in unet.parameters() if p.requires_grad]
    opt = torch.optim.AdamW(params, lr=a.lr, weight_decay=1e-2, betas=(0.9, 0.999))

    step = 0
    scaler = torch.amp.GradScaler("cuda") if device == "cuda" else None
    while step < a.steps:
        for batch in dl:
            if step >= a.steps:
                break
            px = batch["pixel_values"].to(device, dtype=torch.float32)
            ids = batch["input_ids"].to(device)
            mask = batch["attention_mask"].to(device)

            with torch.no_grad():
                latents = vae.encode(px).latent_dist.sample() * vae.config.scaling_factor
                enc = text_encoder(ids, attention_mask=mask)[0]

            noise = torch.randn_like(latents)
            ts = torch.randint(0, noise_scheduler.config.num_train_timesteps, (latents.shape[0],), device=device)
            noisy = noise_scheduler.add_noise(latents, noise, ts)

            with torch.autocast("cuda", dtype=torch.float16, enabled=(device == "cuda")):
                pred = unet(noisy, ts, enc).sample
                if noise_scheduler.config.prediction_type == "v_prediction":
                    target = noise_scheduler.get_velocity(latents, noise, ts)
                else:
                    target = noise
                loss = F.mse_loss(pred.float(), target.float())

            opt.zero_grad(set_to_none=True)
            if scaler is not None:
                scaler.scale(loss).backward()
                scaler.unscale_(opt)
                torch.nn.utils.clip_grad_norm_(params, 1.0)
                scaler.step(opt)
                scaler.update()
            else:
                loss.backward()
                torch.nn.utils.clip_grad_norm_(params, 1.0)
                opt.step()

            step += 1
            if step % 50 == 0 or step == 1:
                print(f"[train] step {step}/{a.steps} loss={loss.item():.4f}", flush=True)

    # Save only the LoRA weights; strip the peft prefix so ComfyUI can load it directly
    state = unet.state_dict()
    lora_state = {}
    for k, v in state.items():
        if "lora_A" in k or "lora_B" in k:
            nk = k.replace("base_model.model.", "").replace(".default", "")
            lora_state[nk] = v.detach().to("cpu")
    from safetensors.torch import save_file
    save_file(lora_state, a.out)
    print(f"[done] wrote {a.out} ({len(lora_state)} tensors)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
