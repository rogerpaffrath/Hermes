/**
* Hermes
* Find the silent spots on your video to make editing easier.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* This project uses FFmpeg - https://ffmpeg.org/
*
* Written by Roger Paffrath, June 2023
*/

#include <iostream>
#include <fstream>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
}

#ifndef HERMES
#define HERMES
    // Adjust this according to your needs.
    #define VIDEO_PATH "test2.mp4"
    #define OUTPUT_PATH "silent_times.txt"
    #define THRESHOLD 0.265
#endif

/**
 * Calculate the energy of an audio frame.
 *
 * @param samples A pointer to the audio samples in the audio frame.
 * @param sample_count The number of audio samples in the frame.
 */
double calculateEnergy(const int16_t* samples, int sample_count) {
    double energy = 0.0;

    for (int i = 0; i < sample_count; ++i) {
        double sample = samples[i] / 32768.0; // Normalize to range [-1, 1]
        energy += sample * sample;
    }

    return energy / sample_count;
}

/**
 * Insert the timestamp into the output file.
 *
 * @param output_file A reference to the output file.
 * @param start_time The starting time the of the silent moment.
 * @param end_time The ending time the of the silent moment.
 */
void insertTimestamp(std::ofstream &output_file, double start_time, double end_time) {
    int start_minutes = floor(start_time / 60.0f);
    int start_seconds = start_time - (start_minutes * 60.0f);

    int end_minutes = floor(end_time / 60.0f);
    int end_seconds = end_time - (start_minutes * 60.0f);

    output_file << "Silent time: " << start_minutes << "m" << start_seconds << "s - " 
                << end_minutes << "m" << end_seconds << "s" << std::endl;
}

int main() {    
    // Open the video file
    AVFormatContext* format_context = nullptr;
    if (avformat_open_input(&format_context, VIDEO_PATH, nullptr, nullptr) != 0) {
        std::cout << "Failed to open video file." << std::endl;
        return -1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(format_context, nullptr) < 0) {
        std::cout << "Failed to retrieve stream information." << std::endl;
        avformat_close_input(&format_context);
        return -1;
    }

    // Find the audio stream
    int audio_stream_index = -1;
    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1) {
        std::cout << "No audio stream found in the video file." << std::endl;
        avformat_close_input(&format_context);
        return -1;
    }

    // Get the audio codec parameters
    AVCodecParameters* codec_parameters = format_context->streams[audio_stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codec_parameters->codec_id);
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);

    avcodec_parameters_to_context(codec_context, codec_parameters);

    // Open the codec
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        std::cout << "Failed to open the audio codec." << std::endl;
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return -1;
    }

    // Open the output file
    std::ofstream output_file(OUTPUT_PATH);
    if (!output_file.is_open()) {
        std::cout << "Failed to open the output file." << std::endl;
        avcodec_free_context(&codec_context);
        avformat_close_input(&format_context);
        return -1;
    }

    // Allocate frame buffers
    AVPacket packet;
    AVFrame* frame = av_frame_alloc();

    // Variables for time calculation
    double frame_rate = av_q2d(format_context->streams[audio_stream_index]->time_base);
    double last_silent_time = -1;
    
    // Read frames and analyze audio
    while (av_read_frame(format_context, &packet) >= 0) {
        if (packet.stream_index == audio_stream_index) {
            int ret = avcodec_send_packet(codec_context, &packet);

            if (ret < 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                std::cout << "avcodec_send_packet: " << ret << std::endl;
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    std::cout << "avcodec_receive_frame: " << ret << std::endl;
                    break;
                }
                else if (ret >= 0) {
                    int sample_count = frame->nb_samples * frame->ch_layout.nb_channels;
                    double energy = calculateEnergy(reinterpret_cast<int16_t*>(frame->data[0]), sample_count);
                        
                    // Check if the frame is silent
                    if (energy <= (double)THRESHOLD) {
                        if (last_silent_time == -1)
                            last_silent_time = (double)frame->pts * frame_rate;
                    }
                    else if (last_silent_time != -1) {
                        double end_time = (double)frame->pts * frame_rate;

                        insertTimestamp(output_file, last_silent_time, end_time);
                        last_silent_time = -1;
                    }
                }
            }
        }

        // Free the packet
        av_packet_unref(&packet);
    }

    if (last_silent_time != -1) {
        double duration = (double)(format_context->streams[audio_stream_index]->duration);
        double ticks = (double)(codec_context->ticks_per_frame);

        double end_time = duration * ticks / (double)(codec_context->time_base.den);

        insertTimestamp(output_file, last_silent_time, end_time);
    }

    // Close the video file and output file
    avcodec_free_context(&codec_context);
    avformat_close_input(&format_context);
    output_file.close();

    std::cout << "Silent times have been saved to 'silent_times.txt'." << std::endl;

    return 0;
}