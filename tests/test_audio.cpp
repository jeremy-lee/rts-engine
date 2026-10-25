// RTS Engine — tests/test_audio.cpp
// M13 验收：音频系统优雅降级（无设备不崩溃）、音量钳制、幂等关闭
#include "test_framework.h"
#include "audio/audio.h"

using namespace rts;

TEST(audio_init_or_graceful_degrade) {
	AudioSystem audio;
	const bool ok = audio.Init();
	// 有音频设备 → 可用；无设备 → 优雅降级（不崩溃）
	if (ok) {
		CHECK(audio.Available());
	} else {
		CHECK(!audio.Available());
	}
	// 不可用时调用播放必须无操作（不崩溃）
	audio.Play(Sfx::Click);
	audio.Play(Sfx::Build);
	audio.Play(Sfx::Attack);
	audio.Play(Sfx::Produce);
	audio.StartMusic();
	audio.StopMusic();
	audio.Shutdown();
}

TEST(audio_volume_clamp) {
	AudioSystem audio;
	audio.Init();
	audio.SetMasterVolume(-5.0f);
	CHECK(audio.MasterVolume() >= 0.0f);
	audio.SetMasterVolume(5.0f);
	CHECK(audio.MasterVolume() <= 1.0f);
	audio.SetMasterVolume(0.42f);
	CHECK(audio.MasterVolume() > 0.41f);
	CHECK(audio.MasterVolume() < 0.43f);
	audio.Shutdown();
}

TEST(audio_shutdown_idempotent) {
	AudioSystem audio;
	audio.Init();
	audio.Shutdown();
	audio.Shutdown(); // 二次关闭不崩溃
	CHECK(!audio.Available());
}

TEST(audio_play_after_shutdown_safe) {
	AudioSystem audio;
	audio.Init();
	audio.Shutdown();
	audio.Play(Sfx::Click);  // 关闭后播放不崩溃
	audio.StartMusic();
	CHECK(!audio.Available());
}
