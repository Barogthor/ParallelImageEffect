#gcc -S src/bitmap.c src/bitmap.h src/info.c src/info.h src/apply-effect.c -Wall -mavx2 -lpthread
gcc src/bitmap.c src/bitmap.h src/info.c src/info.h src/apply-effect.c -o $1 -Wall -lpthread