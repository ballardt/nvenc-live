#ifdef __cplusplus
extern "C" {
#endif

int doStitching( unsigned char* tiledBitstream,
                 int num,
                 unsigned char* bitstream[],
                 int bitstream_Size[],
				 int* tileBitrates,
                 int finalWidth, int finalHeight,
                 int numTileRows, int numTileCols );

extern unsigned char* bitstreams[4];
extern unsigned char* tiledBitstream;

#ifdef __cplusplus
}
#endif	
