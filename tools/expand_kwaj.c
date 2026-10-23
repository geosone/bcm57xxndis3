/* Expand local WfW installation files; no Windows code is executed. */
#include <mspack.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    struct mskwaj_decompressor *decoder;
    int status;
    if (argc != 3) {
        fprintf(stderr, "usage: expand_kwaj INPUT OUTPUT\n");
        return 2;
    }
    decoder = mspack_create_kwaj_decompressor(NULL);
    if (!decoder)
        return 3;
    status = decoder->decompress(decoder, argv[1], argv[2]);
    mspack_destroy_kwaj_decompressor(decoder);
    if (status)
        fprintf(stderr, "%s: KWAJ expansion failed (%d)\n", argv[1], status);
    return status;
}
