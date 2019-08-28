/*
 * v4l2 unmix
 * to separate input from a single device into v4l2loopback specific devices
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

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
#define VIDEO_REQ_BUF_COUNT 10

#define MSTRE(s) #s
#define MSTR(s) MSTRE(s)

static int output_fds[INPUT_NUM];
static int input_fd = -1;

struct buffer {
	void   *start;
	size_t	length;
	int input;
};

struct buffer *buffers;
int frame_size;

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
	//fprintf(stderr, "%d", input);
}

static int read_frame(struct timespec cur_input_begin, struct timespec cur_input_end, 
	int current_input, int fid) {

		struct v4l2_buffer buf;
	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(input_fd, VIDIOC_DQBUF, &buf)) {
		switch (errno) {
			case EAGAIN:
				return 0;

			case EIO:
				/* Could ignore EIO, see spec. */
				/* fall through */

			default:
				errno_exit("VIDIOC_DQBUF");
			}
	}

	assert(buf.index < VIDEO_REQ_BUF_COUNT);

	int input;
	// the buf was captured after switching to the current input?
	if (buf.timestamp.tv_sec >= cur_input_end.tv_sec ||
	   (buf.timestamp.tv_sec == cur_input_end.tv_sec &&
		buf.timestamp.tv_usec >= (cur_input_end.tv_nsec/1000)))
		input = current_input;
	else
		input = (INPUT_NUM+current_input-1)%INPUT_NUM;

	if (buf.bytesused == frame_size && fid > 0)
		process_image(input, buffers[buf.index].start, buf.bytesused);

	if (-1 == xioctl(input_fd, VIDIOC_QBUF, &buf))
        errno_exit("VIDIOC_QBUF");

	return input == current_input;	
}

static void init_mmap(void) {
	struct v4l2_requestbuffers req;

	CLEAR(req);
	req.count = VIDEO_REQ_BUF_COUNT;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl(input_fd, VIDIOC_REQBUFS, &req)) {
		if (EINVAL == errno) {
			fprintf(stderr, "%s does not support memory mapping", input_dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\\n", input_dev_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(req.count, sizeof(*buffers));

	if (!buffers) {
		fprintf(stderr, "Out of memory\\n");
		exit(EXIT_FAILURE);
	}

	for (int bidx= 0; bidx < req.count; ++bidx) {
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type	= V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory	= V4L2_MEMORY_MMAP;
		buf.index	= bidx;

		if (-1 == xioctl(input_fd, VIDIOC_QUERYBUF, &buf))
			errno_exit("VIDIOC_QUERYBUF");

		buffers[bidx].length = buf.length;
		buffers[bidx].start =
			mmap(NULL /* start anywhere */,
			      buf.length,
			      PROT_READ | PROT_WRITE /* required */,
			      MAP_SHARED /* recommended */,
			      input_fd, buf.m.offset);

		if (MAP_FAILED == buffers[bidx].start)
			errno_exit("mmap");
	}
}

static void uninit_device() {
	for (int i = 0; i < VIDEO_REQ_BUF_COUNT; ++i)
		if (-1 == munmap(buffers[i].start, buffers[i].length))
			errno_exit("munmap");	
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

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "%s does not support streaming i/o\\n",
			 input_dev_name);
		exit(EXIT_FAILURE);
	}

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

		frame_size = fmt.fmt.pix.sizeimage;
	}

	init_mmap();
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
		// read frames for each input
		for(int i = 0; i < INPUT_NUM; i++) {

			// select input
			static struct timespec s_input_begin;
			clock_gettime(CLOCK_MONOTONIC, &s_input_begin);
			
			unsigned int idx = inputs[i];
			if (-1 == xioctl(input_fd, VIDIOC_S_INPUT, &idx)) {
				fprintf(stderr, "Unable to select input %u.\n", idx);
				exit(EXIT_FAILURE);
			}

			static struct timespec s_input_end;
			clock_gettime(CLOCK_MONOTONIC, &s_input_end);

			int was_cur_input = 0;
			int min_frames = 3;
			int f = 0;
			do { // while frames are from last input
				was_cur_input = read_frame(s_input_begin, s_input_end, i, f);
				if (was_cur_input)
					f++;
			} while (!was_cur_input || f < min_frames);
		}
	}
}

void start_capturing() {
	for (int i = 0; i < VIDEO_REQ_BUF_COUNT; ++i) {
		struct v4l2_buffer buf;

		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (-1 == xioctl(input_fd, VIDIOC_QBUF, &buf))
				errno_exit("VIDIOC_QBUF");
	}
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl(input_fd, VIDIOC_STREAMON, &type))
			errno_exit("VIDIOC_STREAMON");
}

void stop_capturing() {
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl(input_fd, VIDIOC_STREAMOFF, &type))
		errno_exit("VIDIOC_STREAMOFF");
}

int main(int argc, char *argv[]) {


	open_device(&input_fd, input_dev_name);
	init_input_device();

	for(int i = 0; i < INPUT_NUM; i++)
		open_device(&output_fds[i], output_dev_names[i]);
	init_output_devices();

	start_capturing();
	main_loop();
	stop_capturing();
	
	uninit_device();
	close_device(&input_fd);
	for(int i = 0; i < INPUT_NUM; i++)
		close_device(&output_fds[i]);

	return 0;
}

