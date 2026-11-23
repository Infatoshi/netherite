#include <stdio.h>
#include "../engine/raster.h"

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: raster SCENE OUT.png [COVERAGE.png [SKY.png]]\n");
        return 2;
    }
    return raster_render(argv[1], argv[2], argc >= 4 ? argv[3] : NULL,
                         argc >= 5 ? argv[4] : NULL);
}
