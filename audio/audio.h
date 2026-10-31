// RTS Engine — audio/audio.h
// 音频系统（miniaudio 单头文件）：程序合成音效 + 背景音乐，无外部音频文件。
// 无音频设备时自动降级（不崩溃）。
#pragma once

#include <cstdint>

namespace rts {

enum class Sfx : int {
	Click = 0,     // 选中/点击
	Build = 1,     // 建造完成
	Attack = 2,    // 攻击
	Produce = 3,   // 生产完成
	Move = 4,      // 下达移动命令
	Death = 5,     // 单位死亡
	Explosion = 6, // 爆炸/命中
	Count = 7,
};

class AudioSystem {
public:
	bool Init();
	void Shutdown();

	bool Available() const { return m_available; }

	void Play(Sfx sfx);
	void StartMusic();
	void StopMusic();

	void SetMasterVolume(float v); // 0..1
	float MasterVolume() const { return m_volume; }

private:
	bool m_available = false;
	float m_volume = 0.6f;
	void* m_engine = nullptr;      // ma_engine*
	void* m_music = nullptr;       // ma_sound*
	void* m_sfx[static_cast<int>(Sfx::Count)] = {};
	void* m_buffers[static_cast<int>(Sfx::Count)] = {};
	void* m_sfxPcm[static_cast<int>(Sfx::Count)] = {};
	void* m_musicBuffer = nullptr;
	void* m_musicPcm = nullptr;
};

} // namespace rts
