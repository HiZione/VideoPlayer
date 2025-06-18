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

    // FFmpeg��Դ
    AVFormatContext* m_formatCtx = nullptr;     //���װ������
    AVCodecContext* m_codecCtx = nullptr;       //����������
    const AVCodec* m_codec = nullptr;           //������
    int m_videoStream = -1;                     //Ĭ����Ƶ������
    SwsContext* m_swsCtx = nullptr;
    AVFrame* m_rgbFrame = nullptr;
    uint8_t* m_rgbBuffer = nullptr;

    // ���ſ���
    std::thread m_playThread;       
    std::mutex m_mutex;
    std::atomic<bool> m_playing{ false };
    std::atomic<bool> m_exit{ false };

    // GDI��Դ
    HDC m_memDC = nullptr;
    HBITMAP m_hBitmap = nullptr;
    BITMAPINFO m_bmi = { 0 };
    int m_width = 0;
    int m_height = 0;
};