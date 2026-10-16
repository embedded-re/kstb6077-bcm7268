// video.h - HDMI video brought up by kmon itself (video.c).

int  video_running(void);               // 1 = frame counter moves, 0 = not, -1 = unreadable
void video_status(void);
int  video_init(int force);             // replay BOLT's display script; 0 = display runs
