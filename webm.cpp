#include <opencv2/opencv.hpp>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>	
    #include <libavutil/imgutils.h>
}


static const AVPixelFormat sourcePixelFormat = AV_PIX_FMT_BGR24;
static const AVPixelFormat destPixelFormat = AV_PIX_FMT_YUV420P;
static const AVCodecID destCodec = AV_CODEC_ID_VP8;


int flush_encoder(AVFormatContext *fmt_ctx,unsigned int stream_index){
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2 (fmt_ctx->streams[stream_index]->codec, &enc_pkt,
			NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame){
			ret=0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n",enc_pkt.size);
		/* mux encoded frame */
		ret = av_write_frame(fmt_ctx, &enc_pkt);
		if (ret < 0)
			break;
	}
	return ret;
}



int main(int argc, char** argv)
{
    /**
     * Verify command line arguments
     */
    if (argc != 4) {
        std::cout << "This tool grabs <numberOfFramesToEncode> frames from <input> and encodes a WEBM video with these at <output>" << std::endl;
        std::cout << "Usage: " << argv[0] << " <input> <output> <numberOfFramesToEncode>" << std::endl;
        std::cout << "Sample: " << argv[0] << " sample.avi sample.webm 250" << std::endl;
        exit(1);
    }

    std::string input(argv[1]), output(argv[2]);
    uint32_t framesToEncode;
    std::istringstream(argv[3]) >> framesToEncode;


    cv::VideoCapture videoCapturer(input);
    if (videoCapturer.isOpened() == false) {
        std::cerr << "Cannot open video at '" << input << "'" << std::endl;
        exit(1);
    }

    /**
     * Get some information of the video and print them
     */
    double totalFrameCount = videoCapturer.get(CV_CAP_PROP_FRAME_COUNT);
    unsigned int  width = videoCapturer.get(CV_CAP_PROP_FRAME_WIDTH),
         height = videoCapturer.get(CV_CAP_PROP_FRAME_HEIGHT);

    std::cout << input << "[Width:" << width << ", Height=" << height
              << ", FPS: " << videoCapturer.get(CV_CAP_PROP_FPS)
              << ", FrameCount: " << totalFrameCount << "]" << std::endl;

    /**
     * Be sure we're not asking more frames than there is
     */
    if (framesToEncode > totalFrameCount) {
        std::cerr << "You asked for " << framesToEncode << " but there are only " << totalFrameCount
                  << " frames, will encode as many as there is" << std::endl;
        framesToEncode = totalFrameCount;
    }

    AVFormatContext* pFormatCtx;
	AVOutputFormat* fmt;
	AVStream* video_st;
	AVCodecContext* pCodecCtx;
	AVCodec* pCodec;
	int framecnt=0;
	int framenum=framesToEncode;                                   //Frames to encode
    const char * out_file = output.c_str();
    std::cout<<out_file<<std::endl;
    av_register_all();	
	pFormatCtx = avformat_alloc_context();	
	fmt = av_guess_format(NULL, out_file, NULL);
	pFormatCtx->oformat = fmt;	
	if (avio_open(&pFormatCtx->pb,out_file, AVIO_FLAG_READ_WRITE) < 0){
		printf("Failed to open output file! \n");
		return -1;
	}
	video_st = avformat_new_stream(pFormatCtx, 0);
	if (video_st==NULL){
		return -1;
	}	
	pCodecCtx = video_st->codec;
    pCodecCtx->codec_id = AV_CODEC_ID_VP8;
	pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pCodecCtx->width = width;
	pCodecCtx->height = height;
	pCodecCtx->bit_rate = 400000;
	pCodecCtx->gop_size=1;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;	
	pCodecCtx->max_b_frames=3;	
	AVDictionary *param = 0;	
	if(pCodecCtx->codec_id == AV_CODEC_ID_H264) {
		av_dict_set(&param, "preset", "slow", 0);
		av_dict_set(&param, "tune", "zerolatency", 0);		
	}
	
	if(pCodecCtx->codec_id == AV_CODEC_ID_H265){
		av_dict_set(&param, "preset", "ultrafast", 0);
		av_dict_set(&param, "tune", "zero-latency", 0);
	}	
	av_dump_format(pFormatCtx, 0, out_file, 1);
	pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
	if (!pCodec){
		printf("Can not find encoder! \n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec,&param) < 0){
		printf("Failed to open encoder! \n");
		return -1;
	}	
	avformat_write_header(pFormatCtx,NULL);
	SwsContext *bgr2yuvcontext = sws_getContext(width, height, sourcePixelFormat,
                                                width, height, destPixelFormat,
                                                SWS_BICUBIC, NULL, NULL, NULL);
	for (int i=0; i<framenum; i++){
        std::cout<<"Convert Frame: "<<i<<std::endl;
        cv::Mat cvFrame;
        AVFrame *sourceAvFrame = av_frame_alloc(), *destAvFrame = av_frame_alloc();
        assert(videoCapturer.read(cvFrame) == true);

        /**
         * Allocate source frame, i.e. input to sws_scale()
         */

        av_image_alloc(sourceAvFrame->data, sourceAvFrame->linesize, width, height, sourcePixelFormat, 1);

        /**
         * Copy image data into AVFrame from cv::Mat
         */
        for (uint32_t h = 0; h < height; h++)
            memcpy(&(sourceAvFrame->data[0][h*sourceAvFrame->linesize[0]]), &(cvFrame.data[h*cvFrame.step]), width*3);
        /**
         * Allocate destination frame, i.e. output from sws_scale()
         */
        av_image_alloc(destAvFrame->data, destAvFrame->linesize, width, height, destPixelFormat, 1);
        sws_scale(bgr2yuvcontext, sourceAvFrame->data, sourceAvFrame->linesize,
                  0, height, destAvFrame->data, destAvFrame->linesize);      
        AVPacket avEncodedPacket;
        av_init_packet(&avEncodedPacket);
        avEncodedPacket.data = NULL;
        avEncodedPacket.size = 0;        
        destAvFrame->pts=i*(video_st->time_base.den)/((video_st->time_base.num)*25);		
		int got_picture=0;	
		int ret = avcodec_encode_video2(pCodecCtx, &avEncodedPacket,destAvFrame, &got_picture);
		if(ret < 0){
			printf("Failed to encode! \n");
			return -1;
		}
		if (got_picture==1){
			printf("Succeed to encode frame: %5d\tsize:%5d\n",framecnt,avEncodedPacket.size);
			framecnt++;
			avEncodedPacket.stream_index = video_st->index;
			ret = av_write_frame(pFormatCtx, &avEncodedPacket);
			av_free_packet(&avEncodedPacket);
		}
	}
	int ret = flush_encoder(pFormatCtx,0);
	if (ret < 0) {
		printf("Flushing encoder failed\n");
		return -1;
	}
	//Write file trailer
	av_write_trailer(pFormatCtx);
	//Clean
	if (video_st){
		avcodec_close(video_st->codec);		
	}
	avio_close(pFormatCtx->pb);
	avformat_free_context(pFormatCtx);
	videoCapturer.release();
	return 0;
}
