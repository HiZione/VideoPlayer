#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <Windows.h>  

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    bool Open(const std::string& url);
    void Play();
    void Stop();
    void SetRenderWindow(HWND hwnd);

private:
    void PlayThread();
    bool InitFFmpeg();
    void CleanupFFmpeg();
    void RenderFrame(AVFrame* frame);

private:
    std::string m_url;
    HWND m_hwnd = nullptr;

    // FFmpeg资源
    AVFormatContext* m_formatCtx = nullptr;     //解封装上下文
    AVCodecContext* m_codecCtx = nullptr;       //解码上下文
    const AVCodec* m_codec = nullptr;           //解码器
    int m_videoStream = -1;                     //默认视频流索引
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_rgbFrame = nullptr;
    uint8_t* m_rgbBuffer = nullptr;

    // 播放控制
    std::thread m_playThread;       
    std::mutex m_mutex;
    std::atomic<bool> m_playing{ false };
    std::atomic<bool> m_exit{ false };

    // GDI资源
    HDC m_memDC = nullptr;
    HBITMAP m_hBitmap = nullptr;
    BITMAPINFO m_bmi = { 0 };
    int m_width = 0;
    int m_height = 0;
};