// video.h - HDMI video brought up by kmon itself (video.c).

int  video_running(void);               // 1 = frame counter moves, 0 = not, -1 = unreadable
void video_status(void);
int  video_hdmi_connected(void);       // 1 = TV on the HDMI cable, 0 = none, -1 = unreadable
void video_hdmi_poll(void);           // print a line when that changes (console idle loop)
int  video_init(int force);             // replay BOLT's display script; 0 = display runs
