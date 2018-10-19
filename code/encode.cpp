#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>
#include <boost/dynamic_bitset.hpp>

typedef unsigned char Block;
typedef boost::dynamic_bitset<Block> Bitset;

int getNextNAL(std::ifstream& ifs, unsigned char* buf) {
	// Go past the first border and consume it.
	int zeroCounter = 0;
	char c = 0xFF;
	int bufIdx = 0;
	while (zeroCounter < 2 || (unsigned char)c != 0x01) {
		ifs.read(&c, 1);
		buf[bufIdx] = (unsigned char)c;
		bufIdx++;
		if ((unsigned char)c == 0x00) {
			zeroCounter++;
		}
	}
	// Stop when we encounter the next border. Do not consume it.
	zeroCounter = 0;
	while (zeroCounter < 2 || (unsigned char)c != 0x01) {
		ifs.read(&c, 1);
		buf[bufIdx] = (unsigned char)c;
		bufIdx++;
		if ((unsigned char)c == 0x00) {
			zeroCounter++;
		}
		else if (zeroCounter < 2 || (unsigned char)c != 0x01) {
			zeroCounter = 0;
		}
	}
	ifs.putback((unsigned char) 0x01);
	ifs.putback((unsigned char) 0x00);
	ifs.putback((unsigned char) 0x00);
	return bufIdx-3;
}

void appendCharToBitset(boost::dynamic_bitset<Block>* bits, unsigned char c) {
	for (int i=0; i<8; i++) {
		bits->push_back((c>>i) & 1);
	}
}

int modifySPS(unsigned char* nal, int nalLen) {
	//boost::dynamic_bitset<> x(999);
	Bitset x(999);
	printf("%d\n", nalLen);
	// Convert bytes to bitset
	for (int i=0; i<nalLen; i++) {
		appendCharToBitset(&x, nal[i]);
	}

	// Convert bitset to bytes
	//boost::to_block_range(&x, std::back_inserter(nal));
	std::vector<Block> bytes;
	boost::to_block_range(x, std::back_inserter(bytes));
	//ofs.write((char*), 999);
	return x.size();
}

int main(int, char*[]) {
	std::ifstream ifs("../../videos/stitched.hevc", std::ios::binary);
	std::ofstream ofs("test.hevc", std::ios::binary);
	unsigned char rawNAL[99999];

	int vpsLen = getNextNAL(ifs, rawNAL);
	ofs.write((char*)rawNAL, vpsLen);

	int spsLen = getNextNAL(ifs, rawNAL);
	ofs.write((char*)rawNAL, spsLen);
	printf("%d\n", modifySPS(rawNAL, spsLen));

	int ppsLen = getNextNAL(ifs, rawNAL);
	ofs.write((char*)rawNAL, ppsLen);

	while (true) {
		int nalLen = getNextNAL(ifs, rawNAL);
		ofs.write((char*)rawNAL, nalLen);
	}
}
