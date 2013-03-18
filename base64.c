#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>

#define OPEN_FILE_ERR   0x1
#define MAP_ERR         0x2
#define TEXT_LEN_ERR    0x4
#define DECODE_FLAG     0x1
#define ENCODE_FLAG     0x2
#define FILE_FLAG       0x4
#define HELP_FLAG       0x8

// Translation table to encode
static const char te64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Translation table to decode
static const char td64[128] = { 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
                                127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
                                127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
                                127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
                                127, 127, 127,  62, 127, 127, 127,  63,  52,  53,
                                 54,  55,  56,  57,  58,  59,  60,  61, 127, 127,
                                127,  64, 127, 127, 127,   0,   1,   2,   3,   4,
                                  5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
                                 15,  16,  17,  18,  19,  20,  21,  22,  23,  24,
                                 25, 127, 127, 127, 127, 127, 127,  26,  27,  28,
                                 29,  30,  31,  32,  33,  34,  35,  36,  37,  38,
                                 39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
                                 49,  50,  51, 127, 127, 127, 127, 127 };

static inline __attribute__((always_inline)) void encode_block(unsigned char* in, unsigned char* out, int len)
{
    out[0] = (unsigned char) te64[(int) (in[0] >> 2)];
    out[1] = (unsigned char) te64[(int) (((in[0] & 0x3) << 4) | ((in[1] & 0xF0) >> 4))];
    out[2] = (unsigned char) (len > 1 ? te64[(int) (((in[1] & 0xF) << 2) | ((in[2] & 0xC0) >> 6))] : '=');
    out[3] = (unsigned char) (len > 2 ? te64[(int) (in[2] & 0x3F)] : '=');
}

static int base64_encode(const unsigned char* input, const unsigned char* output, const size_t length)
{
    unsigned char in[3];
    unsigned char out[4];
    int len;
    size_t i, j, k = 0;

    *in = (unsigned char) 0;
    *out = (unsigned char) 0;

    for(i = 0, k = 0; i < length; i+=3, k+=4) {
        len = 0;
        for (j = 0; j < 3; j++) {
            if ((i + j) < length) {
                in[j] = input[i + j];
                len++;
            } else {
                in[j] = 0;
            }
        }
        if (len > 0) {
            encode_block(in, out, len);
            memcpy((void*) &output[k], (void*) out, 4);
        }
    }
    return 0;
}

static inline __attribute__((always_inline)) void decode_block(unsigned char* in, unsigned char* out)
{
    out[0] = (unsigned char) ((td64[(int) in[0]] << 2) | (td64[(int) in[1]] >> 4));
    out[1] = (unsigned char) (in[2] != '=' ? ((td64[(int) in[1]] << 4) | (td64[(int) in[2]] >> 2)) : 0);
    out[2] = (unsigned char) (in[3] != '=' ? (((td64[(int) in[2]] & 0x3) << 6) | td64[(int) in[3]]) : 0);
}

static int base64_decode(const unsigned char* input, const unsigned char* output, const size_t length)
{
    unsigned char* in;
    unsigned char* out;
    unsigned char out_buf[3];
    size_t i;

    in = input;
    out = output;
    *out_buf = (unsigned char) 0;

    for (i = 0; i < length; i+=4, in+=4, out+=3) {
        decode_block(in, out_buf);
        if (in[2] == '=')
            memcpy((void*) out, (void*) out_buf, 1);
        else if(in[3] == '=')
            memcpy((void*) out, (void*) out_buf, 2);
        else
            memcpy((void*) out, (void*) out_buf, 3);
    }

    return 0;
}

static int base64_encode_file(const char* filename, const char* out_filename)
{
    int fd_in, fd_out;
    size_t file_len, out_file_len;
    void *src, *dst;
    clock_t start, end;
    double time_count;

    if ((fd_in = open(filename, O_RDONLY)) == -1)
        return OPEN_FILE_ERR;
    if ((fd_out = open(out_filename, O_RDWR|O_CREAT|O_TRUNC, 00755)) == -1)
        return OPEN_FILE_ERR;

    file_len = lseek(fd_in, 0, SEEK_END);
    lseek(fd_in, 0, SEEK_SET);

    start = clock();

    if ((src = mmap(NULL, file_len, PROT_READ, MAP_SHARED, fd_in, 0)) == -1)
        return MAP_ERR;

    out_file_len = file_len / 3 * 4;
    if (file_len % 3)
        out_file_len += 4;

    lseek(fd_out, out_file_len-1, SEEK_SET);
    write(fd_out, "a", 1);
    lseek(fd_out, 0, SEEK_SET);

    if ((dst = mmap(NULL, out_file_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd_out, 0)) == -1)
        return MAP_ERR;

    base64_encode((unsigned char*) src, (unsigned char*) dst, file_len);

    munmap(dst, out_file_len);
    munmap(src, file_len);
    close(fd_out);
    close(fd_in);

    end = clock();
    time_count = (end - start)/(double)(CLOCKS_PER_SEC);
    printf("Encode file '%s' finished.\n%lf secs, %lf MB/s.\n", filename, time_count, file_len/time_count/1024.0/1024.0);

    return 0;
};

static int base64_decode_file(const char* filename, const char* out_filename)
{
    int fd_in, fd_out;
    size_t file_len, out_file_len;
    void *src, *dst;
    clock_t start, end;
    double time_count;

    if ((fd_in = open(filename, O_RDONLY)) == -1)
        return OPEN_FILE_ERR;
    if ((fd_out = open(out_filename, O_RDWR|O_CREAT|O_TRUNC, 00755)) == -1)
        return OPEN_FILE_ERR;

    file_len = lseek(fd_in, 0, SEEK_END);
    lseek(fd_in, 0, SEEK_SET);

    start = clock();

    if ((src = mmap(NULL, file_len, PROT_READ, MAP_SHARED, fd_in, 0)) == -1)
        return MAP_ERR;

    out_file_len = file_len / 4 * 3;
    if (((unsigned char*) src)[file_len-2] == '=')
        out_file_len -= 2;
    if (((unsigned char*) src)[file_len-1] == '=')
        out_file_len--;

    lseek(fd_out, out_file_len-1, SEEK_SET);
    write(fd_out, "a", 1);
    lseek(fd_out, 0, SEEK_SET);

    if ((dst = mmap(NULL, out_file_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd_out, 0)) == -1)
        return MAP_ERR;

    base64_decode((unsigned char*) src, (unsigned char*) dst, file_len);

    munmap(dst, out_file_len);
    munmap(src, file_len);
    close(fd_out);
    close(fd_in);

    end = clock();
    time_count = (end - start)/(double)(CLOCKS_PER_SEC);
    printf("Decode file '%s' finished.\n%lf secs, %lf MB/s.\n", filename, time_count, file_len/time_count/1024.0/1024.0);

    return 0;
};


const char *short_options = "def:h";
struct option long_options[] = {
    { "decode",     no_argument,  NULL,   'd' },
    { "encode",     no_argument,  NULL,   'e' },
    { "file",       required_argument,  NULL,   'f' },
    { "help",       no_argument,  NULL,   'h' },
    { 0,            0,            0,      0   },
};

const char* help = "help\n";

int main(int argc, char **argv)
{
	int opt;
	char *filename;
	char *out_filename;
	unsigned char flag = 0;
	unsigned char *text;
	int text_len, out_text_len;
	unsigned char *out_text;
	int status_code = 0;

	while((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
        switch(opt) {
            case 'd':
                flag = flag | DECODE_FLAG;
                break;
            case 'e':
                flag = flag | ENCODE_FLAG;
                break;
            case 'f':
                flag = flag | FILE_FLAG;
                filename = optarg;
                break;
            case 'h':
                flag = flag | HELP_FLAG;
                break;
            default:
                fprintf(stderr, "base64: invalid option\n");
                puts(help);
                return 1;
                break;
        }
    }

	if (!((flag & ENCODE_FLAG) || (flag & DECODE_FLAG))) {
        fprintf(stderr, "%s: You must specify '--encode' or '--decode' options\n", argv[0]);
        puts(help);
        return 1;
    }
    if ((flag & ENCODE_FLAG) && (flag & DECODE_FLAG)) {
        fprintf(stderr, "%s: You can not specify '--encode' and '--decode' options at the same time\n", argv[0]);
        puts(help);
        return 1;
    }
	if (argv[optind] == NULL) {
        fprintf(stderr, "%s: missing argument\n", argv[0]);
        puts(help);
        return 1;
    }
    if (argv[optind + 1] != NULL) {
        fprintf(stderr, "%s: redundant argument '%s'\n", argv[0], argv[optind + 1]);
        puts(help);
        return 1;
    }

    if (flag & FILE_FLAG) {
        if (filename == NULL) {
            fprintf(stderr, "%s: No filename, noting to be done\n", argv[0]);
            puts(help);
            return 1;
        }
        out_filename = argv[optind];
        if (flag & ENCODE_FLAG) {
            status_code = base64_encode_file(filename, out_filename);
        } else {
            status_code = base64_decode_file(filename, out_filename);
        }
    } else {
        text = (unsigned char*) argv[optind];
        text_len = strlen((char*) text);
        if (flag & ENCODE_FLAG) {
            out_text_len = text_len / 3 * 4;
            if (text_len % 3)
                out_text_len += 4;
            out_text_len++;
            out_text = (unsigned char*) malloc(out_text_len * sizeof(unsigned char));
            status_code = base64_encode(text, out_text, text_len);
        } else {
            if (text_len < 4) {
                status_code = TEXT_LEN_ERR;
                return 1;
            }
            out_text_len = text_len / 4 * 3;
            if (text[text_len-2] == '=')
                out_text_len -= 2;
            if (text[text_len-1] == '=')
                out_text_len--;
            out_text_len++;
            out_text = (unsigned char*) malloc(out_text_len * sizeof(unsigned char));
            status_code = base64_decode(text, out_text, text_len);
        }
    }

    switch (status_code) {
        case OPEN_FILE_ERR:
            fprintf(stderr, "%s: open file error.\n", argv[0]);
            return 1;
            break;
        case MAP_ERR:
            fprintf(stderr, "%s: mapping file error.\n", argv[0]);
            return 1;
            break;
        case TEXT_LEN_ERR:
            fprintf(stderr, "%s: invalid text.\n", argv[0]);
            return 1;
            break;
        default:
            break;
    }

    if (!(flag & FILE_FLAG)) {
        out_text[out_text_len-1] = 0;
        puts((char*) out_text);
    }

    return 0;
}
