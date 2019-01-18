#pragma once
    
struct Planeset
{
public:
    Planeset( int w, int h )
    {
        y = new unsigned char[w*h];
        u = new unsigned char[w*h/4];
        v = new unsigned char[w*h/4];
    }

    ~Planeset()
    {
        delete [] y;
        delete [] u;
        delete [] v;
    }

	unsigned char* y;
	unsigned char* u;
	unsigned char* v;
};

