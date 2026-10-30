#include "INIReader.h"
#include <cstdio>
int main(int argc, char** argv) {
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        INIReader r(argv[i]);
        bool kb = r.GetBoolean("gui", "androidkeyboard", false);   // mesma chamada de settings.cpp
        std::printf("%s parseError=%d androidkeyboard=%d\n", argv[i], r.ParseError(), (int)kb);
        if (!kb || r.ParseError() < 0) bad++;
    }
    return bad;
}
