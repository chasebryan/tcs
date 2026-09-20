/* Invoked in place of QEMU to check argv, inherited descriptors, and reservation. */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    const char *expected[] = {"-machine","virt,virtualization=on","-cpu","cortex-a53","-m","2G",
        "-smp","1","-display","none","-no-reboot","-chardev","stdio,id=uart,signal=off",
        "-serial","chardev:uart","-monitor","none","-nic","none","-accel","tcg",
        "-global","fw_cfg_mem.dma_enabled=off","-device",NULL,"-fw_cfg",NULL};
    assert(argc == 28);
    for (unsigned i = 0; i < 27; ++i) if (expected[i]) assert(!strcmp(argv[i+1], expected[i]));
    assert(!strcmp(argv[25], "loader,file=launch.img,addr=0x70000000,cpu-num=0,force-raw=on"));
    assert(!strcmp(argv[27], "name=opt/tcs/launch-context,file=launch.used"));
    for (int fd = 3; fd <= 512; ++fd) assert(fcntl(fd, F_GETFD) == -1);
    struct stat s; assert(stat(".", &s) == 0 && (s.st_mode & 07777) == 0700);
    int image = open("launch.img", O_RDONLY), context = open("launch.used", O_RDONLY);
    assert(image >= 3 && context >= 3);
    assert(fstat(image, &s) == 0 && (s.st_mode & 07777) == 0600 && s.st_nlink == 1);
    unsigned char bytes[113]; assert(read(context, bytes, sizeof bytes) == 112);
    assert(!memcmp(bytes, "TCS-LAUNCH\1", 11) && bytes[15] == 1 && bytes[48] == 'B');
    assert(read(image, bytes, sizeof bytes) == 5 && !memcmp(bytes, "IMAGE", 5));
    close(image); close(context);
    puts("PASS exact QEMU arguments, private public-file snapshots, no extra inherited descriptors");
    return getenv("TCS_TEST_PROBE_FAIL") ? 7 : 0;
}
