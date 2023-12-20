#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <sys/file.h>
#include <dirent.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <libudev.h>
#include <stdint.h>
#include <stdarg.h>
#include <linux/uinput.h>
#include <stdlib.h>
#include <fcntl.h>
#include "config.h"
#include "moused.h"
#include <poll.h>


#define UINPUT_DEVICE_NAME "moused virtual device"
#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))

static int ufd = -1;
static int scrollmode = 0;
static float distx = 0;
static float disty = 0;

static struct udev *udev;
static struct udev_monitor *udevmon;

//Active mouse state.
struct mouse {
	int fd;
	char devnode[256];
	struct mouse_config original_cfg;
	struct mouse_config *cfg;
	int button_state[MAX_BUTTONS];

	float sensitivity;
	//misc state

	struct mouse *next;
};

static struct mouse *mice = NULL;
static int debug_flag = 1;

static void warn(char *fmt, ...)
{
	va_list args; 
	va_start(args, fmt);

	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
}

static void die(char *fmt, ...)
{
	va_list args; 
	va_start(args, fmt);

	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(-1);
}

static void dbg(char *fmt, ...)
{
	if(debug_flag) {
		va_list args; 
		va_start(args, fmt);

		vfprintf(stderr, fmt, args);
		va_end(args);
	}
}

static int is_mouse(struct udev_device *dev) 
{
	const char *path = udev_device_get_devnode(dev);
	if(!path || !strstr(path, "event")) //Filter out non evdev devices.
		return 0;

	struct udev_list_entry *prop;
	udev_list_entry_foreach(prop, udev_device_get_properties_list_entry(dev)) {
		if(!strcmp(udev_list_entry_get_name(prop), "ID_INPUT_MOUSE") &&
		   !strcmp(udev_list_entry_get_value(prop), "1")) {
			return 1;
		}
	}

	return 0;
}

static int ipc_exec(int type, const char *data, size_t sz, uint32_t timeout)
{
	struct ipc_message msg;

	assert(sz <= sizeof(msg.data));

	msg.type = type;
	msg.sz = sz;
	msg.timeout = timeout;
	memcpy(msg.data, data, sz);

	int con = ipc_connect();

	if (con < 0) {
		perror("connect");
		exit(-1);
	}

	xwrite(con, &msg, sizeof msg);
	xread(con, &msg, sizeof msg);

	if (msg.sz) {
		xwrite(1, msg.data, msg.sz);
		xwrite(1, "\n", 1);
	}

	return msg.type == IPC_FAIL;

	return type == IPC_FAIL;
}

void xwrite(int fd, const void *buf, size_t sz)
{
	size_t nwr = 0;
	ssize_t n;

	while(sz != nwr) {
		n = write(fd, buf+nwr, sz-nwr);
		if (n < 0) {
			perror("write");
			exit(-1);
		}
		nwr += n;
	}
}

void xread(int fd, void *buf, size_t sz)
{
	size_t nrd = 0;
	ssize_t n;

	while(sz != nrd) {
		n = read(fd, buf+nrd, sz-nrd);
		if (n < 0) {
			perror("read");
			exit(-1);
		}
		nrd += n;
	}
}

static void get_mouse_nodes(char *nodes[MAX_MICE], int *sz) 
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *ent;

	udev = udev_new();
	if (!udev)
		die("Cannot create udev context.");

	enumerate = udev_enumerate_new(udev);
	if (!enumerate)
		die("Cannot create enumerate context.");

	udev_enumerate_add_match_subsystem(enumerate, "input");
	udev_enumerate_add_match_subsystem(enumerate, "input");
	udev_enumerate_scan_devices(enumerate);

	devices = udev_enumerate_get_list_entry(enumerate);
	if (!devices)
		die("Failed to get device list.");

	*sz = 0;
	udev_list_entry_foreach(ent, devices) {
		const char *name = udev_list_entry_get_name(ent);;
		struct udev_device *dev = udev_device_new_from_syspath(udev, name);
		const char *path = udev_device_get_devnode(dev);

		if(is_mouse(dev)) {
			nodes[*sz] = malloc(strlen(path)+1);
			strcpy(nodes[*sz], path);
			(*sz)++;
			assert(*sz <= MAX_MICE);
		}

		udev_device_unref(dev);
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);
}

static int create_uinput_fd() 
{
	struct uinput_setup usetup;

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if(fd < 0) {
		perror("open");
		exit(-1);
	}

	ioctl(fd, UI_SET_EVBIT, EV_REL);
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);
	ioctl(fd, UI_SET_EVBIT, EV_MSC);

	ioctl(fd, UI_SET_RELBIT, REL_X);
	ioctl(fd, UI_SET_RELBIT, REL_Y);
	ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
	ioctl(fd, UI_SET_RELBIT, REL_HWHEEL);

        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
        ioctl(fd, UI_SET_KEYBIT, BTN_4);
        ioctl(fd, UI_SET_KEYBIT, BTN_5);
        ioctl(fd, UI_SET_KEYBIT, BTN_6);
        ioctl(fd, UI_SET_KEYBIT, BTN_7);
        ioctl(fd, UI_SET_KEYBIT, BTN_SIDE);
        ioctl(fd, UI_SET_KEYBIT, BTN_EXTRA);
        ioctl(fd, UI_SET_KEYBIT, BTN_FORWARD);
        ioctl(fd, UI_SET_KEYBIT, BTN_BACK);
        ioctl(fd, UI_SET_KEYBIT, BTN_TASK);

	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x046d;
	usetup.id.product = 0xc52b;
	strcpy(usetup.name, UINPUT_DEVICE_NAME);

	ioctl(fd, UI_DEV_SETUP, &usetup);
	ioctl(fd, UI_DEV_CREATE);

	return fd;
}

int evdev_code_to_button(int code)
{
	switch(code) {
	case BTN_LEFT: return 1;
	case BTN_MIDDLE: return 2;
	case BTN_RIGHT: return 3;
	case BTN_4: return 4;
	case BTN_5: return 5;
	case BTN_6: return 6;
	case BTN_7: return 7;
	case BTN_SIDE: return 8;
	case BTN_EXTRA: return 9;
	case BTN_FORWARD: return 10;
	case BTN_BACK: return 11;
	case BTN_TASK: return 12;
	default: 
		       dbg("Unrecognized mouse button %d\n", code);
		       return 0;
	}
}

void write_event(int fd, uint16_t type, uint16_t code, int32_t value) 
{
	struct input_event ev;

	ev.type = type;
	ev.code = code;
	ev.value = value;
	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;

	write(fd, &ev, sizeof(ev));
}

void send_button_event(int btn, int ispressed)
{
	int code;

	switch (btn) {
	case 1: code = BTN_LEFT;break;
	case 2: code = BTN_MIDDLE;break;
	case 3: code = BTN_RIGHT;break;
	case 4: code = BTN_4;break;
	case 5: code = BTN_5;break;
	case 6: code = BTN_6;break;
	case 7: code = BTN_7;break;
	case 8: code = BTN_SIDE;break;
	case 9: code = BTN_EXTRA;break;
	case 10: code = BTN_FORWARD;break;
	case 11: code = BTN_BACK;break;
	case 12: code = BTN_TASK;break;
	default: 
		 die("Unrecognized button %d", btn);
		 break;
	}

	write_event(ufd, EV_KEY, code, ispressed);
}

/* ispressed = 2 indicates a stateless trigger (i.e a scroll event).
 * In the case of ACTION_BUTTON_[0-9] this corresponds to a complete
 * button press (mouse down + mouse up). */

void perform_action(struct mouse *mouse, struct action *action, int ispressed)
{
	int btn;

	switch(action->type) {
	case ACTION_SET_SENSITIVITY:
		if(!ispressed) break;
		mouse->sensitivity = mouse->sensitivity == action->val ? mouse->cfg->sensitivity : action->val;
		break;
	case ACTION_SET_SCROLLMODE:
		if(!ispressed) break;

		if(action->val == 2)
			scrollmode = !scrollmode;
		else
			scrollmode = action->val;

		distx = 0;
		disty = 0;
		break;
	case ACTION_BUTTON_TOGGLE:
		if(ispressed) break;

		btn = (int)action->val;

		ispressed = !mouse->button_state[btn-1];
		mouse->button_state[btn-1] = ispressed;

		send_button_event(btn, ispressed);
		break;
	case ACTION_BUTTON:
		btn = (int)action->val;
		mouse->button_state[btn-1] = ispressed;

		if(ispressed == 2) { //If we are not processing a button event simulate a complete click.
			send_button_event(btn, 1);
			write_event(ufd, EV_SYN, SYN_REPORT, 0);
			send_button_event(btn, 0);
		} else {
			send_button_event(btn, ispressed);
		}
		break;
	default:
		die("Unknown action encountered: %d", action->type);
	}
}

void process_event(struct mouse *mouse, struct input_event *ev)
{
	ev->input_event_sec = 0;
	ev->input_event_usec = 0;

	switch(ev->type) {
		struct action *action;
		int btn;
		int ispressed;

	case EV_KEY:
		if(ev->value == 2) //Ignore syn events.
			break;

		ispressed = ev->value;
		btn = evdev_code_to_button(ev->code);
		if(!btn) {
			warn("Unrecognized button %d\n", ev->value);
			break;
		}

		action = &mouse->cfg->buttons[btn-1];

		if(action->type != ACTION_DEFAULT) {
			perform_action(mouse, action, ispressed);
		} else {
			mouse->button_state[btn-1] = ispressed;
			write(ufd, ev, sizeof(*ev));
		}

		break;
	case EV_REL:
		switch(ev->code) {
		case REL_WHEEL:
			action = ev->value == -1 ? &mouse->cfg->scroll_down : &mouse->cfg->scroll_up;

			if(action->type != ACTION_DEFAULT)
				perform_action(mouse, action, 2);
			else {
				ev->value *= mouse->cfg->scroll_sensitivity;
				write(ufd, ev, sizeof(*ev));
			}

			break;
		case REL_HWHEEL:
			action = ev->value == -1 ? &mouse->cfg->scroll_right : &mouse->cfg->scroll_left;

			if(action->type != ACTION_DEFAULT)
				perform_action(mouse, action, 2);
			else {
				ev->value *= mouse->cfg->scroll_sensitivity;
				write(ufd, ev, sizeof(*ev));
			}

			break;
		case REL_Y:
		case REL_X:
			ev->value *= mouse->sensitivity;

			if(scrollmode) {
				float sensitivity = 1/mouse->cfg->scrollmode_sensitivity;

				if(ev->code == REL_Y || ev->code == REL_X) { 
					if(mouse->cfg->scroll_invert_axes)
						ev->value *= -1;

					if(mouse->cfg->scroll_swap_axes)
						ev->code = ev->code == REL_X ? REL_Y : REL_X;
				}

				if(!mouse->cfg->scroll_inhibit_y && ev->code == REL_Y) {
					disty += ev->value;

					while(abs(disty) > abs(sensitivity)) {
						disty -= disty < 0 ? -sensitivity : sensitivity;
						write_event(ufd, EV_REL, REL_WHEEL, disty < 0 ? 1 : -1);
					}

					write_event(ufd, EV_SYN, SYN_REPORT, 0);
				} else if(!mouse->cfg->scroll_inhibit_x && ev->code == REL_X) {
					distx += ev->value;

					while(abs(distx) > abs(sensitivity)) {
						distx -= distx < 0 ? -sensitivity : sensitivity;
						write_event(ufd, EV_REL, REL_HWHEEL, distx < 0 ? -1 : 1);
					}

					write_event(ufd, EV_SYN, SYN_REPORT, 0);
				}
			} else
				write(ufd, ev, sizeof(*ev));

			break;
		default:
			//warn("Unrecognized REL event: %d\n", ev->code);
			write(ufd, ev, sizeof(*ev));
			break;
		} 
		break;
	default:
		write(ufd, ev, sizeof(*ev));
	}
}

const char *evdev_device_name(const char *devnode)
{
	static char name[256];

	int fd = open(devnode, O_RDONLY);
	if(fd < 0) {
		perror("open");
		exit(-1);
	}

	if(ioctl(fd, EVIOCGNAME(sizeof(name)), &name) == -1) {
		perror("ioctl");
		exit(-1);
	}

	close(fd);
	return name;
}

int manage_mouse(const char *devnode)
{
	int fd;
	const char *name = evdev_device_name(devnode);
	struct mouse *mouse;
	struct mouse_config *cfg = NULL;

	for(cfg = configs;cfg;cfg = cfg->next) {
		if(!strcmp(cfg->name, name))
			break;
	}

	if(!cfg) { //Don't manage mice for which there is no configuration.
		dbg("No config found for %s, ignoring\n", name);
		return 0;
	}

	if((fd = open(devnode, O_RDONLY | O_NONBLOCK)) < 0) {
		perror("open");
		exit(1);
	}

	mouse = malloc(sizeof(struct mouse));
	mouse->fd = fd;
	mouse->cfg = cfg;
	mouse->original_cfg = *cfg;
	mouse->sensitivity = mouse->cfg->sensitivity;
	strcpy(mouse->devnode, devnode);

	//Grab the mouse.
	if(ioctl(fd, EVIOCGRAB, (void *)1) < 0) {
		perror("EVIOCGRAB"); 
		exit(-1);
	}

	mouse->next = mice;
	mice = mouse;

	dbg("Managing %s\n", evdev_device_name(devnode));
	return 1;
}

int destroy_mouse(const char *devnode)
{
	struct mouse **ent = &mice;

	while(*ent) {
		if(!strcmp((*ent)->devnode, devnode)) {
			dbg("Destroying %s\n", devnode);
			struct mouse *m = *ent;
			*ent = m->next;

			//Attempt to ungrab the the mouse (assuming it still exists)
			if(ioctl(m->fd, EVIOCGRAB, (void *)1) < 0) {
				perror("EVIOCGRAB"); 
			}

			close(m->fd);
			free(m);

			return 1;
		}

		ent = &(*ent)->next;
	}

	return 0;
}

void evdev_monitor_loop(int *fds, int sz) 
{
	struct input_event ev;
	fd_set fdset;
	int i;
	char names[256][256];

	for(i = 0;i < sz;i++) {
		int fd = fds[i];
		if(ioctl(fd, EVIOCGNAME(sizeof(names[fd])), names[fd]) == -1) {
			perror("ioctl");
			exit(-1);
		}
	}

	while(1) {
		int i;
		int maxfd = fds[0];

		FD_ZERO(&fdset);
		for(i = 0;i < sz;i++) {
			if(maxfd < fds[i]) maxfd = fds[i];
			FD_SET(fds[i], &fdset);
		}

		select(maxfd+1, &fdset, NULL, NULL, NULL);

		for(i = 0;i < sz;i++) {
			int fd = fds[i];
			if(FD_ISSET(fd, &fdset)) {
				while(read(fd, &ev, sizeof(ev)) > 0) {
					if(ev.type == EV_SYN)
						continue;
					printf("%s: ", names[fd]);
					switch(ev.type) {
						int btn;

					case EV_REL:
						switch(ev.code) {
						case REL_X:
							printf("REL X: %d", ev.value);
							break;
						case REL_Y:
							printf("REL Y: %d", ev.value);
							break;
						case REL_WHEEL: //Treat as buttons 4 and 5
							printf("Scroll %s: %d", ev.value == 1 ? "up" : "down", ev.value);
							break;
						case REL_HWHEEL:
							printf("Scroll %s: %d", ev.value == 1 ? "left" : "right", ev.value);
							break;
						default:
							printf("UNKNOWN REL EV: %d", ev.code);
							break;
						}
						break;
					case EV_KEY:
						btn = evdev_code_to_button(ev.code);
						printf("Button: %d %d", btn, ev.value);
						break;
					default:
						printf("Unrecognized event type: %d", ev.type);
					}
					printf("\n");
				}
			}
		}
	}
}

int monitor_loop() 
{
	char *devnodes[256];
	int sz, i;
	int fd = -1;
	int fds[256];
	int nfds = 0;

	get_mouse_nodes(devnodes, &sz);

	for(i = 0;i < sz;i++) {
		fd = open(devnodes[i], O_RDONLY | O_NONBLOCK);
		if(fd < 0) {
			perror("open");
			exit(-1);
		}
		fds[nfds++] = fd;
	}

	evdev_monitor_loop(fds, nfds);

	return 0;
}


static void send_fail(int con, const char *fmt, ...)
{
	struct ipc_message msg = {0};
	va_list args;

	va_start(args, fmt);

	msg.type = IPC_FAIL;
	msg.sz = vsnprintf(msg.data, sizeof(msg.data), fmt, args);

	xwrite(con, &msg, sizeof msg);
	close(con);

	va_end(args);
}

static void send_success(int con)
{
	struct ipc_message msg = {0};

	msg.type = IPC_SUCCESS;;
	msg.sz = 0;

	xwrite(con, &msg, sizeof msg);
	close(con);
}


int add_remove_bindings(struct mouse_config *mouse_config, const char *exp)
{
	if (!strcmp(exp, "reset")) {
		//printf("\nresetting config\n");
		*mice->cfg = mice->original_cfg;
		return 0;
	} else {
		//printf("\nadding bindings\n");
		return config_add_entry(mice->cfg, exp);
	}
}

static void handle_client(int con)
{
	struct ipc_message msg;

	xread(con, &msg, sizeof msg);

	if (msg.sz >= sizeof(msg.data)) {
		send_fail(con, "maximum message size exceeded");
		return;
	}
	msg.data[msg.sz] = 0;

	if (msg.timeout > 1000000) {
		send_fail(con, "timeout cannot exceed 1000 ms");
		return;
	}

	switch (msg.type) {
		struct mouse_config *ent;
		int success;

	case IPC_BIND:
		success = 0;

		if (msg.sz == sizeof(msg.data)) {
			send_fail(con, "bind expression size exceeded");
			return;
		}

		msg.data[msg.sz] = 0;

		for (ent = configs; ent; ent = ent->next) {
			//printf("\n%s\n", msg.data);
			if(!(add_remove_bindings(ent, msg.data)))
				success = 1;
		}

		if (success)
			send_success(con);
		else
			send_fail(con, "failed");

		break;
	default:
		send_fail(con, "Unknown command");
		break;
	}
}

static long get_time_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1E3 + ts.tv_nsec / 1E6;
}

static int ipcfd = -1;

void main_loop() 
{
	ipcfd = ipc_create_server(SOCKET_PATH);
	if (ipcfd < 0)
		die("failed to create %s (another instance already running?)", SOCKET_PATH);

	struct mouse *mouse;
	int monfd;
	int timeout = 0;

	int i, sz;
	char *devs[MAX_MICE];

	get_mouse_nodes(devs, &sz);

	for(i = 0;i < sz;i++) {
		manage_mouse(devs[i]);
		free(devs[i]);
	}

	udev = udev_new();
	udevmon = udev_monitor_new_from_netlink(udev, "udev");

	if (!udev)
		die("Can't create udev.");

	udev_monitor_filter_add_match_subsystem_devtype(udevmon, "input", NULL);
	udev_monitor_enable_receiving(udevmon);

	monfd = udev_monitor_get_fd(udevmon);

	struct pollfd pfds[100];

	pfds[0].fd = ipcfd;
	pfds[0].events = POLLIN | POLLERR;

	while(1) {
		int maxfd;
		fd_set fds;
		struct udev_device *dev;

		FD_ZERO(&fds);
		FD_SET(monfd, &fds);

		maxfd = monfd;

		for(mouse = mice;mouse;mouse=mouse->next) {
			int fd = mouse->fd;

			maxfd = maxfd > fd ? maxfd : fd;
			FD_SET(fd, &fds);
		}

		if(select(maxfd+1, &fds, NULL, NULL, NULL) > 0) {
			if(FD_ISSET(monfd, &fds)) {
				dev = udev_monitor_receive_device(udevmon);

				const char *devnode = udev_device_get_devnode(dev);

				if(devnode && is_mouse(dev)) {
					const char *action = udev_device_get_action(dev);

					if(!strcmp(action, "add"))
						manage_mouse(devnode);
					else if(!strcmp(action, "remove"))
						destroy_mouse(devnode);
				}
				udev_device_unref(dev);
			}

			int j = 1;
			for(mouse = mice;mouse;mouse=mouse->next) {
				int fd = mouse->fd;
				pfds[j].fd = mouse->fd;
				pfds[j].events = POLLIN | POLLERR;
				j++;
			}

			int start_time;
			int end_time;
			int elapsed;
			start_time = get_time_ms();
			poll(pfds, j+1, timeout > 0 ? timeout : -1);
			end_time = get_time_ms();
			elapsed = end_time - start_time;

			timeout-=elapsed;
			
			int k = 1;
			for(mouse = mice;mouse;mouse=mouse->next) {
				if (pfds[k].revents)
				{
					int fd = mouse->fd;
					if (FD_ISSET(fd, &fds))
					{
						struct input_event ev;

						if (read(fd, &ev, sizeof(ev)) > 0)
						{
							process_event(mouse, &ev);
						}
					}
				}
				k++;
			}

			if (pfds[0].revents)
			{
				int con = accept(ipcfd, NULL, 0);
				if (con < 0)
				{
					perror("accept");
					exit(-1);
				}

				handle_client(con);
			}
		}
	}
}

void cleanup()
{
	struct mouse *mouse = mice;
	struct mouse_config *cfg = configs;

	udev_unref(udev);
	udev_monitor_unref(udevmon);

	while(mouse) {
		struct mouse *tmp = mouse;
		mouse = mouse->next;
		free(tmp);
	}

	while(cfg) {
		struct mouse_config *tmp = cfg;
		cfg = cfg->next;
		free(tmp);
	}
}

void lock()
{
	int fd;

	if((fd=open(LOCK_FILE, O_CREAT | O_RDWR, 0600)) == -1) {
		perror("open");
		exit(1);
	}

	if(flock(fd, LOCK_EX | LOCK_NB) == -1)
		die("Another instance of moused is already running.");
}


void exit_signal_handler(int sig)
{
	warn("%s received, cleaning up and termianting...", sig == SIGINT ? "SIGINT" : "SIGTERM");

	cleanup();
	exit(0);
}

static int add_bindings(int argc, char *argv[])
{
	int i;
	int ret = 0;
	for (i = 1; i < argc; i++) {
		if (ipc_exec(IPC_BIND, argv[i], strlen(argv[i]), 0))
			ret = -1;	
	}

	if (!ret)
		printf("Success\n");

	return ret;
}

struct {
	const char *name;
	const char *flag;
	const char *long_flag;

	int (*fn)(int argc, char **argv);
} commands[] = {
	{"monitor"	, "-m", "--monitor"		, monitor_loop},
	{"bind"		, "-e", "--expression"	, add_bindings},
};

int main(int argc, char *argv[])
{
	size_t i;

	if (argc > 1) {
		for (i = 0; i < ARRAY_SIZE(commands); i++)
			if (!strcmp(commands[i].name, argv[1]) ||
				!strcmp(commands[i].flag, argv[1]) ||
				!strcmp(commands[i].long_flag, argv[1])) {
				return commands[i].fn(argc - 1, argv + 1);
			}
	}

	// if(argc > 1 && !strcmp(argv[1], "-m"))
	// 	return monitor_loop();

	lock();

	signal(SIGINT, exit_signal_handler);
	signal(SIGTERM, exit_signal_handler);

	if(access(CONFIG_PATH, F_OK))
		die(CONFIG_PATH" does not exist");

	ufd = create_uinput_fd();
	parse_config_file(CONFIG_PATH);

	main_loop();
}
