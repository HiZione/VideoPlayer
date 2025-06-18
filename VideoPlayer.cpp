#include "VideoPlayer.h"
#include <Windows.h>
#include <stdexcept>
#include <iostream>
#include <chrono>

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")

VideoPlayer::VideoPlayer() {
    avformat_network_init();
}

VideoPlayer::~VideoPlayer() {
    Stop();
    CleanupFFmpeg();
    avformat_network_deinit();
}

bool VideoPlayer::Open(const std::string& url) {
    // 先停止并清理
    Stop();
    CleanupFFmpeg();

    // 确保线程完全退出
    if (m_playThread.joinable()) {
        m_playThread.join();
    }

    m_url = url;
    return InitFFmpeg();
}

void VideoPlayer::SetRenderWindow(HWND hwnd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hwnd = hwnd;
}

void VideoPlayer::Play() {
    if (m_playing) return;
    m_playing = true;
    m_exit = false;
    m_playThread = std::thread(&VideoPlayer::PlayThread, this);
}

void VideoPlayer::Stop() {
    if (!m_playing) return;

    m_playing = false;
    m_exit = true;

    if (m_playThread.joinable()) {
        m_playThread.join();
    }
}

bool VideoPlayer::InitFFmpeg() {
    // 创建格式上下文
    m_formatCtx = avformat_alloc_context();
    if (!m_formatCtx) {
        std::cerr << "无法分配格式上下文" << std::endl;
        return false;
    }

    // 打开输入
    if (avformat_open_input(&m_formatCtx, m_url.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "无法打开文件: " << m_url << std::endl;
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
        return false;
    }

    // 获取流信息
    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        std::cerr << "无法获取流信息" << std::endl;
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 查找视频流
    m_videoStream = -1;
    for (unsigned int i = 0; i < m_formatCtx->nb_streams; i++) {
        if (m_formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStream = i;
            break;
        }
    }

    if (m_videoStream == -1) {
        std::cerr << "未找到视频流" << std::endl;
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 获取解码器
    AVCodecParameters* codecParams = m_formatCtx->streams[m_videoStream]->codecpar;
    m_codec = avcodec_find_decoder(codecParams->codec_id);
    if (!m_codec) {
        std::cerr << "未找到解码器" << std::endl;
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 创建解码器上下文
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
        std::cerr << "无法分配解码器上下文" << std::endl;
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 复制参数
    if (avcodec_parameters_to_context(m_codecCtx, codecParams) < 0) {
        std::cerr << "无法复制编解码器参数" << std::endl;
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 打开解码器
    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        std::cerr << "无法打开解码器" << std::endl;
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 打印视频信息
    std::cout << "视频信息: "
        << codecParams->width << "x" << codecParams->height
        << " 格式: " << av_get_pix_fmt_name(m_codecCtx->pix_fmt)
        << " 帧率: " << av_q2d(m_formatCtx->streams[m_videoStream]->avg_frame_rate) << " fps"
        << std::endl;

    // 设置尺寸
    m_width = codecParams->width;
    m_height = codecParams->height;

    // 创建缩放上下文
    m_swsCtx = sws_getContext(
        m_width, m_height, m_codecCtx->pix_fmt,
        m_width, m_height, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_swsCtx) {
        std::cerr << "无法创建缩放上下文" << std::endl;
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 分配RGB帧
    m_rgbFrame = av_frame_alloc();
    if (!m_rgbFrame) {
        std::cerr << "无法分配RGB帧" << std::endl;
        sws_freeContext(m_swsCtx);
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 分配RGB缓冲区
    int rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_width, m_height, 1);
    m_rgbBuffer = (uint8_t*)av_malloc(rgbBufferSize * sizeof(uint8_t));
    if (!m_rgbBuffer) {
        std::cerr << "无法分配RGB缓冲区" << std::endl;
        av_frame_free(&m_rgbFrame);
        sws_freeContext(m_swsCtx);
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 填充RGB帧
    if (av_image_fill_arrays(
        m_rgbFrame->data, m_rgbFrame->linesize,
        m_rgbBuffer, AV_PIX_FMT_BGRA,
        m_width, m_height, 1) < 0) {
        std::cerr << "无法填充RGB帧" << std::endl;
        av_free(m_rgbBuffer);
        av_frame_free(&m_rgbFrame);
        sws_freeContext(m_swsCtx);
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_formatCtx);
        return false;
    }

    // 清理旧的GDI资源
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
    }
    if (m_memDC) {
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }

    // 创建新的GDI资源
    HDC hdcScreen = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(hdcScreen);
    if (!m_memDC) {
        std::cerr << "无法创建内存DC" << std::endl;
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    // 初始化位图信息
    ZeroMemory(&m_bmi, sizeof(BITMAPINFO));
    m_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    m_bmi.bmiHeader.biWidth = m_width;
    m_bmi.bmiHeader.biHeight = -m_height; // 从上到下
    m_bmi.bmiHeader.biPlanes = 1;
    m_bmi.bmiHeader.biBitCount = 32;
    m_bmi.bmiHeader.biCompression = BI_RGB;

    // 创建DIB段
    void* bits = nullptr;
    m_hBitmap = CreateDIBSection(
        hdcScreen, &m_bmi, DIB_RGB_COLORS,
        &bits, nullptr, 0);

    if (!m_hBitmap) {
        std::cerr << "无法创建位图" << std::endl;
        DeleteDC(m_memDC);
        m_memDC = nullptr;
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    // 将位图选入内存DC
    SelectObject(m_memDC, m_hBitmap);
    ReleaseDC(nullptr, hdcScreen);

    // 确保RGB缓冲区指向位图数据
    if (bits) {
        m_rgbBuffer = static_cast<uint8_t*>(bits);
        // 更新RGB帧指向新的缓冲区
        av_image_fill_arrays(
            m_rgbFrame->data, m_rgbFrame->linesize,
            m_rgbBuffer, AV_PIX_FMT_BGRA,
            m_width, m_height, 1);
    }

    return true;
}

void VideoPlayer::CleanupFFmpeg() {
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }

    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }

    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
        m_formatCtx = nullptr;
    }

    // 注意：不再释放 m_rgbBuffer，因为它现在由位图拥有
    if (m_rgbFrame) {
        av_frame_free(&m_rgbFrame);
        m_rgbFrame = nullptr;
    }

    // 位图现在拥有RGB缓冲区，所以只释放位图
    if (m_hBitmap) {
        DeleteObject(m_hBitmap);
        m_hBitmap = nullptr;
        // 缓冲区由位图管理，所以置空但不释放
        m_rgbBuffer = nullptr;
    }

    if (m_memDC) {
        DeleteDC(m_memDC);
        m_memDC = nullptr;
    }

    m_videoStream = -1;
    m_width = 0;
    m_height = 0;
}

void VideoPlayer::PlayThread() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    if (!packet || !frame) {
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        m_playing = false;
        return;
    }

    double fps = av_q2d(m_formatCtx->streams[m_videoStream]->avg_frame_rate);
    int frameDelay = (fps > 0) ? static_cast<int>(1000 / fps) : 40;

    auto startTime = std::chrono::steady_clock::now();

    while (!m_exit) {
        int ret = av_read_frame(m_formatCtx, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                // 循环播放
                av_seek_frame(m_formatCtx, m_videoStream, 0, AVSEEK_FLAG_BACKWARD);
                startTime = std::chrono::steady_clock::now();
                continue;
            }
            else if (m_exit) {
                // 请求退出
                break;
            }
            else {
                // 其他错误
                std::cerr << "读取帧错误: " << ret << std::endl;
                break;
            }
        }

        if (packet->stream_index == m_videoStream) {
            ret = avcodec_send_packet(m_codecCtx, packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                av_packet_unref(packet);
                continue;
            }

            while (true) {
                ret = avcodec_receive_frame(m_codecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    std::cerr << "解码错误" << std::endl;
                    break;
                }

                // 转换帧格式
                sws_scale(
                    m_swsCtx,
                    frame->data, frame->linesize,
                    0, m_height,
                    m_rgbFrame->data, m_rgbFrame->linesize);

                // 渲染帧
                RenderFrame(m_rgbFrame);

                // 控制帧率
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
                int sleepTime = frameDelay - static_cast<int>(elapsed);

                if (sleepTime > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
                }

                startTime = now;
            }
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    m_playing = false;
}

void VideoPlayer::RenderFrame(AVFrame* frame) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_hwnd || !IsWindow(m_hwnd)) {
        return;
    }

    // 确保有有效的缓冲区
    if (!m_rgbBuffer || !frame || !frame->data[0]) {
        return;
    }

    // 复制数据到位图
    memcpy(m_rgbBuffer, frame->data[0], m_width * m_height * 4);

    HDC hdc = GetDC(m_hwnd);
    if (!hdc) return;

    RECT rect;
    GetClientRect(m_hwnd, &rect);
    int winWidth = rect.right - rect.left;
    int winHeight = rect.bottom - rect.top;

    float aspectRatio = static_cast<float>(m_width) / m_height;
    int renderWidth = winWidth;
    int renderHeight = static_cast<int>(winWidth / aspectRatio);

    if (renderHeight > winHeight) {
        renderHeight = winHeight;
        renderWidth = static_cast<int>(winHeight * aspectRatio);
    }

    int xOffset = (winWidth - renderWidth) / 2;
    int yOffset = (winHeight - renderHeight) / 2;

    // 使用内存DC中的位图
    SetStretchBltMode(hdc, HALFTONE);
    StretchBlt(
        hdc, xOffset, yOffset, renderWidth, renderHeight,
        m_memDC, 0, 0, m_width, m_height, SRCCOPY);

    ReleaseDC(m_hwnd, hdc);
}