// RTS Engine — audio/audio.cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio.h"
#include <cmath>
#include <vector>
#include <cstdio>

namespace rts {

namespace {

constexpr int kSampleRate = 48000;

// 生成单音（正弦/方波混合），带淡入淡出
std::vector<float> MakeTone(float freq, float seconds, float volume, bool square = false) {
	const int n = static_cast<int>(kSampleRate * seconds);
	std::vector<float> out(static_cast<size_t>(n), 0.0f);
	const int fade = n / 10 + 1;
	for (int i = 0; i < n; ++i) {
		const float t = static_cast<float>(i) / kSampleRate;
		float v = std::sin(2.0f * 3.14159265f * freq * t);
		if (square) v = (v >= 0.0f) ? 1.0f : -1.0f;
		// 淡入淡出
		float env = 1.0f;
		if (i < fade) env = static_cast<float>(i) / fade;
		if (i > n - fade) env = static_cast<float>(n - i) / fade;
		out[static_cast<size_t>(i)] = v * volume * env;
	}
	return out;
}

// 生成简单旋律（循环段落）
std::vector<float> MakeMusicLoop() {
	// C4 E4 G4 C5 G4 E4（A 小调感觉）
	const float notes[] = {261.63f, 329.63f, 392.00f, 523.25f, 392.00f, 329.63f};
	const float noteDur = 0.25f;
	const int noteSamples = static_cast<int>(kSampleRate * noteDur);
	std::vector<float> out;
	out.reserve(static_cast<size_t>(noteSamples) * 6);
	for (float f : notes) {
		const auto tone = MakeTone(f, noteDur, 0.18f, false);
		out.insert(out.end(), tone.begin(), tone.end());
	}
	return out;
}

} // namespace

bool AudioSystem::Init() {
	auto* engine = new ma_engine();
	if (ma_engine_init(nullptr, engine) != MA_SUCCESS) {
		delete engine;
		m_available = false;
		return false;
	}
	m_engine = engine;

	// 预生成音效
	struct SfxDef {
		float freq;
		float seconds;
		float volume;
		bool square;
	};
	const SfxDef defs[] = {
		{880.0f, 0.06f, 0.35f, true},   // Click
		{523.25f, 0.18f, 0.35f, false}, // Build
		{220.0f, 0.12f, 0.35f, true},   // Attack
		{659.25f, 0.15f, 0.35f, false}, // Produce
		{440.0f, 0.05f, 0.22f, true},   // Move（短促确认音）
		{146.83f, 0.30f, 0.30f, false}, // Death（低沉下滑）
		{98.00f, 0.22f, 0.40f, true},   // Explosion（低频噪声感）
	};

	for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
		auto* pcm = new std::vector<float>(MakeTone(defs[i].freq, defs[i].seconds, defs[i].volume, defs[i].square));
		auto* buffer = new ma_audio_buffer();
		ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
			ma_format_f32, 1, pcm->size(), pcm->data(), nullptr);
		if (ma_audio_buffer_init(&cfg, buffer) != MA_SUCCESS) {
			delete buffer;
			delete pcm;
			continue;
		}
		auto* sound = new ma_sound();
		if (ma_sound_init_from_data_source(static_cast<ma_engine*>(m_engine), buffer,
				MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, sound) != MA_SUCCESS) {
			delete sound;
			ma_audio_buffer_uninit(buffer);
			delete buffer;
			delete pcm;
			continue;
		}
		m_sfx[i] = sound;
		m_buffers[i] = buffer;
		m_sfxPcm[i] = pcm;
	}

	// 背景音乐
	{
		auto* pcm = new std::vector<float>(MakeMusicLoop());
		auto* buffer = new ma_audio_buffer();
		ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
			ma_format_f32, 1, pcm->size(), pcm->data(), nullptr);
		if (ma_audio_buffer_init(&cfg, buffer) == MA_SUCCESS) {
			auto* music = new ma_sound();
			if (ma_sound_init_from_data_source(static_cast<ma_engine*>(m_engine), buffer,
					MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, music) == MA_SUCCESS) {
				ma_sound_set_looping(music, MA_TRUE);
				m_music = music;
				m_musicPcm = pcm;
				m_musicBuffer = buffer;
			} else {
				delete music;
				ma_audio_buffer_uninit(buffer);
				delete buffer;
				delete pcm;
			}
		} else {
			delete buffer;
			delete pcm;
		}
	}

	m_available = true;
	return true;
}

void AudioSystem::Shutdown() {
	StopMusic();
	auto* engine = static_cast<ma_engine*>(m_engine);
	if (engine != nullptr) {
		for (int i = 0; i < static_cast<int>(Sfx::Count); ++i) {
			if (m_sfx[i] != nullptr) {
				auto* sound = static_cast<ma_sound*>(m_sfx[i]);
				ma_sound_uninit(sound);
				delete sound;
				m_sfx[i] = nullptr;
			}
			if (m_buffers[i] != nullptr) {
				auto* buffer = static_cast<ma_audio_buffer*>(m_buffers[i]);
				ma_audio_buffer_uninit(buffer);
				delete buffer;
				m_buffers[i] = nullptr;
			}
			if (m_sfxPcm[i] != nullptr) {
				delete static_cast<std::vector<float>*>(m_sfxPcm[i]);
				m_sfxPcm[i] = nullptr;
			}
		}
		if (m_music != nullptr) {
			auto* sound = static_cast<ma_sound*>(m_music);
			ma_sound_uninit(sound);
			delete sound;
			m_music = nullptr;
		}
		if (m_musicBuffer != nullptr) {
			auto* buffer = static_cast<ma_audio_buffer*>(m_musicBuffer);
			ma_audio_buffer_uninit(buffer);
			delete buffer;
			m_musicBuffer = nullptr;
		}
		if (m_musicPcm != nullptr) {
			delete static_cast<std::vector<float>*>(m_musicPcm);
			m_musicPcm = nullptr;
		}
		ma_engine_uninit(engine);
		delete engine;
		m_engine = nullptr;
	}
	m_available = false;
}

void AudioSystem::Play(Sfx sfx) {
	if (!m_available || m_engine == nullptr) return;
	const int idx = static_cast<int>(sfx);
	if (idx < 0 || idx >= static_cast<int>(Sfx::Count) || m_sfx[idx] == nullptr) return;
	auto* sound = static_cast<ma_sound*>(m_sfx[idx]);
	ma_sound_seek_to_pcm_frame(sound, 0);
	ma_sound_start(sound);
}

void AudioSystem::StartMusic() {
	if (!m_available || m_music == nullptr) return;
	auto* music = static_cast<ma_sound*>(m_music);
	if (!ma_sound_is_playing(music)) {
		ma_sound_seek_to_pcm_frame(music, 0);
		ma_sound_start(music);
	}
}

void AudioSystem::StopMusic() {
	if (m_music == nullptr) return;
	auto* music = static_cast<ma_sound*>(m_music);
	ma_sound_stop(music);
}

void AudioSystem::SetMasterVolume(float v) {
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	m_volume = v;
	auto* engine = static_cast<ma_engine*>(m_engine);
	if (engine != nullptr) {
		ma_engine_set_volume(engine, v);
	}
}

} // namespace rts
