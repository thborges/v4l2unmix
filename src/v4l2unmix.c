/*
 * v4l2 unmix
 * to separate input from a single device into v4l2loopback specific devices
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

static char *input_dev_name = "/dev/video0";

#define INPUT_NUM 2 
static int inputs[INPUT_NUM] = {0, 1};
static char *output_dev_names[INPUT_NUM] = {
		"/dev/video1",
		"/dev/video2"};

#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480
#define VIDEO_PIX_FMT V4L2_PIX_FMT_YUV420
#define VIDEO_FIELD V4L2_FIELD_INTERLACED
#define VIDEO_STD 

#define MSTRE(s) #s
#define MSTR(s) MSTRE(s)

static int output_fds[INPUT_NUM];
static int input_fd = -1;

struct buffer {
	void   *start;
	size_t	length;
};

struct buffer buf;

#define CLEAR(x) memset(&(x), 0, sizeof(x))

static void errno_exit(const char *s) {
	fprintf(stderr, "%s error %d, %s\\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg) {
	int r;
	
	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);
	
	return r;
}

static void process_image(int input, const void *buf, unsigned int length) {

	unsigned int w = write(output_fds[input], buf, length);
	if (w != length) {
		fprintf(stderr, "Unable to write frame to output %d: %u bytes to write, %u written.\n", input,
			length, w);
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "%d", input);
}

static int read_frame(int input) {
	if (-1 == read(input_fd, buf.start, buf.length)) {
		switch (errno) {
			case EAGAIN:
				return 0;
			case EIO:
				//fall through
			default:
				errno_exit("read");
		}
	}
	process_image(input, buf.start, buf.length);
	return 0;	
}

static void init_read(unsigned int buffer_size) {
	buf.length = buffer_size;
	buf.start = malloc(buffer_size);

	if (!buf.start) {
		fprintf(stderr, "Out of memory\\n");
		exit(EXIT_FAILURE);
	}
}

static void uninit_device() {
	free(buf.start);
}

static void init_output_devices(void) {
	struct v4l2_format fmt;
	CLEAR(fmt);

	for(int i = 0; i < INPUT_NUM; i++) {
		fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		fmt.fmt.pix.width		= VIDEO_WIDTH;
		fmt.fmt.pix.height		= VIDEO_HEIGHT;
		fmt.fmt.pix.pixelformat = VIDEO_PIX_FMT;
		fmt.fmt.pix.field		= VIDEO_FIELD;

		if (-1 == xioctl(output_fds[i], VIDIOC_S_FMT, &fmt)) {
			fprintf(stderr, "Unable to set video format %dx%d, %s, %s for output %d.\n", 
				fmt.fmt.pix.width, fmt.fmt.pix.height, MSTR(VIDEO_PIX_FMT), MSTR(VIDEO_FIELD), i);
			exit(EXIT_FAILURE);
		}

		fprintf(stderr, "Format for output %u:%s will be %dx%d, %s, %s, frame with %d bytes.\n",
			i, output_dev_names[i], fmt.fmt.pix.width, fmt.fmt.pix.height,
			MSTR(VIDEO_PIX_FMT), MSTR(VIDEO_FIELD), fmt.fmt.pix.sizeimage);

	}
}

static void init_input_device(void) {
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	unsigned int min;

	if (-1 == xioctl(input_fd, VIDIOC_QUERYCAP, &cap)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s is no V4L2 device\\n", input_dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\\n", input_dev_name);
		exit(EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
		fprintf(stderr, "%s does not support read i/o\\n", input_dev_name);
		exit(EXIT_FAILURE);
	}

	/*case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf(stderr, "%s does not support streaming i/o\\n",
				 input_dev_name);
			exit(EXIT_FAILURE);
		}
		break;
	}*/


	/* Select video input, video standard and tune here. */
	assert(sizeof(inputs) > 0 && "should specify at least one input");
	for(int i = 0; i < INPUT_NUM; i++) {
		struct v4l2_input input;
		struct v4l2_standard standard;
		CLEAR(input);
		input.index = inputs[i];
		if (-1 == xioctl(input_fd, VIDIOC_ENUMINPUT, &input)) {
				fprintf(stderr, "Unable to query input %d.", input.index);
				exit(EXIT_FAILURE);
		}
		if (input.type & V4L2_INPUT_TYPE_TUNER)
			fprintf(stderr, "Input %u:%s is a TUNER.\n", input.index, input.name);
		if (input.type & V4L2_INPUT_TYPE_CAMERA)
			fprintf(stderr, "Input %u:%s is a CAMERA.\n", input.index, input.name);
		if (-1 == xioctl(input_fd, VIDIOC_S_INPUT, &input.index)) {
				fprintf(stderr, "Unable to select input %u:%s.\n", input.index, input.name);
				exit(EXIT_FAILURE);
		}

		// set norm
		int std_id = V4L2_STD_NTSC;
		//int std_id = V4L2_STD_SECAM;
		//int std_id = V4L2_STD_PAL;
		if (-1 == xioctl(input_fd, VIDIOC_S_STD, &std_id)) {
				fprintf(stderr, "Unable to set video standard %d for input %d.\n", 
					std_id, input.index);
				exit(EXIT_FAILURE);
		}

		CLEAR(cropcap);

		cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (0 == xioctl(input_fd, VIDIOC_CROPCAP, &cropcap)) {
			crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			crop.c = cropcap.defrect; /* reset to default */

			if (-1 == xioctl(input_fd, VIDIOC_S_CROP, &crop)) {
				switch (errno) {
				case EINVAL:
					/* Cropping not supported. */
					break;
				default:
					/* Errors ignored. */
					break;
				}
			}
		} else {
		/* Errors ignored. */
		}


		CLEAR(fmt);
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt.fmt.pix.width		= VIDEO_WIDTH;
		fmt.fmt.pix.height		= VIDEO_HEIGHT;
		fmt.fmt.pix.pixelformat = VIDEO_PIX_FMT;
		fmt.fmt.pix.field		= VIDEO_FIELD;

		if (-1 == xioctl(input_fd, VIDIOC_S_FMT, &fmt)) {
			fprintf(stderr, "Unable to set video format %dx%d, %s, %s for input %d.\n", 
				fmt.fmt.pix.width, fmt.fmt.pix.height, MSTR(VIDEO_PIX_FMT), MSTR(VIDEO_FIELD), 
				input.index);
			exit(EXIT_FAILURE);
		}

		fprintf(stderr, "Format for input %u:%s will be %dx%d, %s, %s, frame with %d bytes.\n",
			input.index, input.name, fmt.fmt.pix.width, fmt.fmt.pix.height,
			MSTR(VIDEO_PIX_FMT), MSTR(VIDEO_FIELD), fmt.fmt.pix.sizeimage);
	}

	init_read(fmt.fmt.pix.sizeimage);
}

static void close_device(int *dfd)
{
	if (-1 == close(*dfd))
		errno_exit("close");

	*dfd = -1;
}

static void open_device(int *dfd, char *dev_name)
{
	struct stat st;

	if (-1 == stat(dev_name, &st)) {
		fprintf(stderr, "Cannot identify '%s': %d, %s\\n",
			 dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!S_ISCHR(st.st_mode)) {
		fprintf(stderr, "%s is no device", dev_name);
		exit(EXIT_FAILURE);
	}

	*dfd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

	if (-1 == *dfd) {
		fprintf(stderr, "Cannot open '%s': %d, %s\\n",
			 dev_name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

static void main_loop() {
	fd_set fds;
	struct timeval tv;
	int r;

	while (1) {
		// read one frame from each input
		for(int i = 0; i < INPUT_NUM; i++) {

			// select input
			struct v4l2_input input;
			CLEAR(input);
			input.index = inputs[i];
			if (-1 == xioctl(input_fd, VIDIOC_S_INPUT, &input.index)) {
				fprintf(stderr, "Unable to select input %u:%s.\n", input.index, input.name);
				exit(EXIT_FAILURE);
			}

			int frames_per_input = 2;
			for(int f = 0; f < frames_per_input; f++) {

				do {
					FD_ZERO(&fds);
					FD_SET(input_fd, &fds);

					/* Timeout. */
					tv.tv_sec = 2;
					tv.tv_usec = 0;

					r = select(input_fd + 1, &fds, NULL, NULL, &tv);
				} while ((r == -1 && errno == EINTR));

				if (r == -1)
					errno_exit("select");
		
				if (r == 0)
					errno_exit("select timeout\n");

				if (read_frame(inputs[i]))
					break;
			}
		}
	}
}

int main(int argc, char *argv[]) {


	open_device(&input_fd, input_dev_name);
	init_input_device();

	for(int i = 0; i < INPUT_NUM; i++)
		open_device(&output_fds[i], output_dev_names[i]);
	init_output_devices();

	main_loop();
	
	uninit_device();
	close_device(&input_fd);
	for(int i = 0; i < INPUT_NUM; i++)
		close_device(&output_fds[i]);

	return 0;
}

