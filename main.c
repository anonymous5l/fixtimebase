#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>

static int
ff_copy_streams(AVFormatContext *ctx, AVFormatContext *octx) {
	int i, ret;
	AVStream *in_stream, *out_stream;

	ret = 0;

	if (ctx->nb_streams > octx->nb_streams)
	{
		for (i = octx->nb_streams; i < ctx->nb_streams; i++)
		{
			in_stream = ctx->streams[i];

			out_stream = avformat_new_stream(octx, NULL);

			ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);

			if (ret < 0) {
				return ret;
			}

			out_stream->codecpar->codec_tag = 0;
		}
	}

	return ret;
}

static int
ff_copy(const char *source, const char *dst) {
	AVFormatContext *input, *output;
	input = NULL; output = NULL;
	AVStream *in, *out;
	in = NULL; out = NULL;
	int64_t *pts_start, *dts_start;
	pts_start = NULL; dts_start = NULL;

	int ret, wHead;
	ret = 0; wHead = 0;

	if ((input = avformat_alloc_context()) == NULL) {
		ret = -1;
	}

	if ((ret = avformat_open_input(&input, source, NULL, NULL)) < 0) {
		goto _failed;
	}

	if ((ret = avformat_find_stream_info(input, NULL)) < 0) {
		goto _failed;
	}

	if ((ret = avformat_alloc_output_context2(&output, NULL, NULL, dst)) < 0) {
		goto _failed;
	}

	if ((ret = ff_copy_streams(input, output)) < 0) {
		goto _failed;
	}

	pts_start = malloc(sizeof(int64_t) * output->nb_streams);
	memset(pts_start, 0, sizeof(int64_t) * output->nb_streams);

	dts_start = malloc(sizeof(int64_t) * output->nb_streams);
	memset(dts_start, 0, sizeof(int64_t) * output->nb_streams);

	while (1) {
		AVPacket pkt;
		av_init_packet(&pkt);

		if ((ret = av_read_frame(input, &pkt)) < 0) {
			break;
		}

		if (pkt.size) {
			if (pkt.stream_index < output->nb_streams) {
				in = input->streams[pkt.stream_index];
				out = output->streams[pkt.stream_index];

				if (pts_start[pkt.stream_index] == 0) {
					if (pkt.pts == 0) {
						ret = -2;
						break;
					}

					pts_start[pkt.stream_index] = pkt.pts;
				}

				if (dts_start[pkt.stream_index] == 0) {
					if (pkt.dts == 0) {
						ret = -2;
						break;
					}

					if (in->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
						dts_start[pkt.stream_index] = pkt.pts;
					} else {
						// audio & video not same logic don't know why
						dts_start[pkt.stream_index] = pkt.dts;
					}
				}

				if (wHead == 0) {
					if ((ret = avio_open(&output->pb, dst, AVIO_FLAG_WRITE)) < 0) {
						break;
					}

					if ((ret = avformat_write_header(output, NULL)) < 0) {
						break;
					}

					wHead = 1;
				}

				// fix timebase

				pkt.pts -= pts_start[pkt.stream_index];
				pkt.dts -= dts_start[pkt.stream_index];

				pkt.pts = av_rescale_q_rnd(pkt.pts, in->time_base, out->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
				pkt.dts = av_rescale_q_rnd(pkt.dts, in->time_base, out->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
				pkt.duration = av_rescale_q(pkt.duration, in->time_base, out->time_base);

				pkt.pos = -1;

				if ((ret = av_interleaved_write_frame(output, &pkt)) < 0) {
					break;
				}
			}
		}

		av_packet_unref(&pkt);
	}

	av_write_trailer(output);

	if (pts_start != NULL) {
		free(pts_start);
	}

	if (dts_start != NULL) {
		free(dts_start);
	}

_failed:
	if (input != NULL) {
		avformat_close_input(&input);
	}

	if (output != NULL) {
		
		if (!(output->flags & AVFMT_NOFILE)) {
			avio_closep(&output->pb);
		}

		avformat_free_context(output);
	}

	return ret;
}

static char *
get_filename_ext(const char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return NULL;
    return dot;
}

int
main(int argc, char **argv) {
	if (argc <= 1) {
		printf("please input file path!\n");
		return 1;
	}

	avdevice_register_all();
	avformat_network_init();

	int source_path_len, ext_len, ret;
	char *source_path = NULL;
	char *ext = NULL;
	char error[256];
	memset(error, 0, 256);
	char dst_path[1024];
	memset(dst_path, 0, 1024);

	for(int i = 0; i < argc - 1; i++) {
		source_path = argv[i + 1];
		source_path_len = strlen(source_path);

		if (source_path != NULL) {
			ext = get_filename_ext(source_path);
			ext_len = strlen(ext);

			if (ext != NULL) {
				// inlcude \0
				snprintf(dst_path, 1024, "%.*s_convert%s", source_path_len - ext_len, source_path, ext);

				printf("Processing `%s`\n", source_path);

				ret = ff_copy(source_path, dst_path);

				if (ret == -1) {
					printf("Alloc context failed!\n");
					return ret;
				} else if (ret == -2) {
					printf("No more need fix!\n");
				} else if (ret == 0) {
					printf("Fixed `%s`!\n", dst_path);

					// remove origin file 
					remove(source_path);
				} else {
					av_strerror(ret, error, 255);
					printf("ffmpeg: ret: %d err: %s\n", ret, error);
				}
			} else {
				printf("Processing `%s` failed! can't get file extension\n", source_path);
			}
		}
	}

	return 0;
}


// 17 1-11