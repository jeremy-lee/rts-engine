// RTS Engine — tools/inflate.h
// DEFLATE（RFC 1951）解压，零依赖。算法结构与 zlib 的参考实现 puff.c 相同：
//   - LSB-first 位流
//   - stored / fixed-Huffman / dynamic-Huffman 三种块
//   - canonical Huffman：count[16] + symbol[288]
// 只做解压（.oramap 是 zip，需要它），不做压缩。
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace inflate {

namespace detail {

inline const uint16_t kLenBase[29] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
	35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
inline const uint8_t kLenExtra[29] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
	3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline const uint16_t kDistBase[30] = {
	1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
	257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
inline const uint8_t kDistExtra[30] = {
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
	7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Huffman {
	short count[16] = {};
	short symbol[288] = {};
};

class BitReader {
public:
	BitReader(const uint8_t* data, size_t len) : m_data(data), m_len(len) {}

	// 读 n 位（LSB first），返回 false 表示数据耗尽
	bool Bits(int need, int& out) {
		while (m_bitCount < need) {
			if (m_pos >= m_len) return false;
			m_bitBuf |= static_cast<unsigned>(m_data[m_pos++]) << m_bitCount;
			m_bitCount += 8;
		}
		out = static_cast<int>(m_bitBuf & ((1u << need) - 1u));
		m_bitBuf >>= need;
		m_bitCount -= need;
		return true;
	}

	bool Bit(int& out) { return Bits(1, out); }

	// 丢弃到字节边界，返回已消费字节数
	size_t AlignToByte() {
		m_bitBuf = 0;
		m_bitCount = 0;
		return m_pos;
	}

	const uint8_t* Data() const { return m_data; }
	size_t Pos() const { return m_pos; }
	void SetPos(size_t p) { m_pos = p; m_bitBuf = 0; m_bitCount = 0; }

private:
	const uint8_t* m_data = nullptr;
	size_t m_len = 0;
	size_t m_pos = 0;
	unsigned m_bitBuf = 0;
	int m_bitCount = 0;
};

// 由码长构造 canonical Huffman；返回 <0 表示非法（超订），0 表示完整，>0 表示不完整但可用
inline int Construct(Huffman& h, const short* length, int n) {
	for (int len = 0; len <= 15; ++len) h.count[len] = 0;
	for (int symbol = 0; symbol < n; ++symbol) h.count[length[symbol]]++;
	if (h.count[0] == n) return 0;

	int left = 1;
	for (int len = 1; len <= 15; ++len) {
		left <<= 1;
		left -= h.count[len];
		if (left < 0) return left;
	}
	short offs[16] = {};
	offs[1] = 0;
	for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<short>(offs[len] + h.count[len]);
	for (int symbol = 0; symbol < n; ++symbol) {
		if (length[symbol] != 0) h.symbol[offs[length[symbol]]++] = static_cast<short>(symbol);
	}
	return left;
}

inline int Decode(BitReader& r, const Huffman& h) {
	int code = 0;
	int first = 0;
	int index = 0;
	for (int len = 1; len <= 15; ++len) {
		int bit = 0;
		if (!r.Bit(bit)) return -1;
		code |= bit;
		const int count = h.count[len];
		if (code - count < first) return h.symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	return -10;
}

} // namespace detail

// 解压 DEFLATE 流到 out（自动扩容）。成功返回 true。
inline bool Inflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
	using namespace detail;
	out.clear();
	out.reserve(inLen * 3 + 1024);
	BitReader reader(in, inLen);

	int last = 0;
	while (!last) {
		int type = 0;
		if (!reader.Bits(1, last)) return false;
		if (!reader.Bits(2, type)) return false;

		if (type == 0) {
			// stored：对齐到字节，读 LEN/NLEN，然后整段拷贝
			const size_t pos = reader.AlignToByte();
			if (pos + 4 > inLen) return false;
			const uint16_t len = static_cast<uint16_t>(in[pos] | (in[pos + 1] << 8));
			const uint16_t nlen = static_cast<uint16_t>(in[pos + 2] | (in[pos + 3] << 8));
			if (static_cast<uint16_t>(len ^ 0xFFFFu) != nlen) return false;
			if (pos + 4 + len > inLen) return false;
			out.insert(out.end(), in + pos + 4, in + pos + 4 + len);
			reader.SetPos(pos + 4 + len);
			continue;
		}

		Huffman lenCode;
		Huffman distCode;
		if (type == 1) {
			// fixed Huffman
			short lengths[288];
			int i = 0;
			for (; i < 144; ++i) lengths[i] = 8;
			for (; i < 256; ++i) lengths[i] = 9;
			for (; i < 280; ++i) lengths[i] = 7;
			for (; i < 288; ++i) lengths[i] = 8;
			Construct(lenCode, lengths, 288);
			for (i = 0; i < 30; ++i) lengths[i] = 5;
			Construct(distCode, lengths, 30);
		} else if (type == 2) {
			// dynamic Huffman
			int hlit = 0, hdist = 0, hclen = 0;
			if (!reader.Bits(5, hlit) || !reader.Bits(5, hdist) || !reader.Bits(4, hclen)) return false;
			hlit += 257;
			hdist += 1;
			hclen += 4;
			if (hlit > 286 || hdist > 30) return false;

			static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
			short codeLengths[19] = {};
			for (int i = 0; i < hclen; ++i) {
				int v = 0;
				if (!reader.Bits(3, v)) return false;
				codeLengths[order[i]] = static_cast<short>(v);
			}
			Huffman lenLenCode;
			if (Construct(lenLenCode, codeLengths, 19) < 0) return false;

			short lengths[288 + 30] = {};
			int index = 0;
			while (index < hlit + hdist) {
				const int sym = Decode(reader, lenLenCode);
				if (sym < 0) return false;
				if (sym < 16) {
					lengths[index++] = static_cast<short>(sym);
				} else if (sym == 16) {
					if (index == 0) return false;
					int rep = 0;
					if (!reader.Bits(2, rep)) return false;
					const short prev = lengths[index - 1];
					for (int k = 0; k < 3 + rep; ++k) {
						if (index >= hlit + hdist) return false;
						lengths[index++] = prev;
					}
				} else if (sym == 17) {
					int rep = 0;
					if (!reader.Bits(3, rep)) return false;
					for (int k = 0; k < 3 + rep; ++k) {
						if (index >= hlit + hdist) return false;
						lengths[index++] = 0;
					}
				} else if (sym == 18) {
					int rep = 0;
					if (!reader.Bits(7, rep)) return false;
					for (int k = 0; k < 11 + rep; ++k) {
						if (index >= hlit + hdist) return false;
						lengths[index++] = 0;
					}
				} else {
					return false;
				}
			}
			if (Construct(lenCode, lengths, hlit) < 0) return false;
			if (Construct(distCode, lengths + hlit, hdist) < 0) return false;
		} else {
			return false; // type 3 保留
		}

		// 解压数据
		for (;;) {
			const int sym = Decode(reader, lenCode);
			if (sym < 0) return false;
			if (sym < 256) {
				out.push_back(static_cast<uint8_t>(sym));
			} else if (sym == 256) {
				break; // end of block
			} else {
				const int li = sym - 257;
				if (li >= 29) return false;
				int extra = 0;
				if (kLenExtra[li] != 0) {
					if (!reader.Bits(kLenExtra[li], extra)) return false;
				}
				const int length = kLenBase[li] + extra;

				const int dsym = Decode(reader, distCode);
				if (dsym < 0 || dsym >= 30) return false;
				int dextra = 0;
				if (kDistExtra[dsym] != 0) {
					if (!reader.Bits(kDistExtra[dsym], dextra)) return false;
				}
				const int dist = kDistBase[dsym] + dextra;
				if (dist <= 0 || static_cast<size_t>(dist) > out.size()) return false;
				const size_t src = out.size() - static_cast<size_t>(dist);
				for (int k = 0; k < length; ++k) out.push_back(out[src + k]);
			}
		}
	}
	return true;
}

} // namespace inflate
