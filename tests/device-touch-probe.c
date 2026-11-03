#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int send_event(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event event = {0};
    event.type = type; event.code = code; event.value = value;
    return write(fd, &event, sizeof(event)) == sizeof(event);
}
static int point(int fd, int x, int y) {
    /* X7's configured 270-degree transform and 113-pixel raw X offset. */
    return send_event(fd, EV_ABS, ABS_MT_POSITION_X, y + 113) &&
           send_event(fd, EV_ABS, ABS_MT_POSITION_Y, 959 - x) &&
           send_event(fd, EV_ABS, ABS_X, y + 113) &&
           send_event(fd, EV_ABS, ABS_Y, 959 - x) &&
           send_event(fd, EV_SYN, SYN_REPORT, 0);
}
int main(int argc, char **argv) {
    int fd = open("/dev/input/by-path/hyn_ts", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open touch device"); return 1; }
    if (argc == 1) {
        for (int code = ABS_MT_SLOT; code <= ABS_MT_TRACKING_ID; ++code) {
            struct input_absinfo info;
            if (ioctl(fd, EVIOCGABS(code), &info) == 0)
                printf("abs %d min=%d max=%d value=%d\n", code, info.minimum, info.maximum, info.value);
        }
        int slots[6] = {ABS_MT_TRACKING_ID, 0, 0, 0, 0, 0};
        if (ioctl(fd, EVIOCGMTSLOTS(sizeof(slots)), slots) == 0)
            for (int i = 0; i < 5; ++i) printf("slot %d tracking=%d\n", i, slots[i+1]);
        close(fd); return 0;
    }
    if (argc != 3 && argc != 6) { fprintf(stderr, "usage: %s [x y [endX endY milliseconds]]\n", argv[0]); return 2; }
    int x = atoi(argv[1]), y = atoi(argv[2]);
    int ex = argc == 6 ? atoi(argv[3]) : x, ey = argc == 6 ? atoi(argv[4]) : y;
    int duration = argc == 6 ? atoi(argv[5]) : 100;
    if (x < 0 || x >= 960 || ex < 0 || ex >= 960 || y < 0 || y >= 266 || ey < 0 || ey >= 266 || duration < 50 || duration > 5000) return 2;
    int ok = send_event(fd, EV_ABS, ABS_MT_SLOT, 0) &&
             send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, 12000 + getpid() % 10000) &&
             send_event(fd, EV_KEY, BTN_TOUCH, 1) && point(fd, x, y);
    int steps = duration / 20;
    for (int i = 1; ok && i <= steps; ++i) {
        usleep(20000);
        ok = point(fd, x + (ex - x) * i / steps, y + (ey - y) * i / steps);
    }
    int released = send_event(fd, EV_ABS, ABS_MT_SLOT, 0) &&
                   send_event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1) &&
                   send_event(fd, EV_KEY, BTN_TOUCH, 0) && send_event(fd, EV_SYN, SYN_REPORT, 0);
    close(fd);
    if (!ok || !released) { perror("write touch event"); return 1; }
    return 0;
}
