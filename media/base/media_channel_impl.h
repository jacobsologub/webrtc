/*
 *  Copyright 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_BASE_MEDIA_CHANNEL_IMPL_H_
#define MEDIA_BASE_MEDIA_CHANNEL_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/audio_options.h"
#include "api/call/audio_sink.h"
#include "api/call/transport.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/frame_transformer_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "media/base/stream_params.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/dscp.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/socket.h"
#include "rtc_base/thread_annotations.h"
// This file contains the base classes for classes that implement
// the MediaChannel interfaces.
// These implementation classes used to be the exposed interface names,
// but this is in the process of being changed.
// TODO(bugs.webrtc.org/13931): Consider removing these classes.

// The target

namespace cricket {

class VoiceMediaChannel;
class VideoMediaChannel;

class MediaChannel : public MediaSendChannelInterface,
                     public MediaReceiveChannelInterface {
 public:
  explicit MediaChannel(webrtc::TaskQueueBase* network_thread,
                        bool enable_dscp = false);
  virtual ~MediaChannel();

  // Downcasting to the subclasses.
  virtual VideoMediaChannel* AsVideoChannel() {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }

  virtual VoiceMediaChannel* AsVoiceChannel() {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }

  // Must declare the methods inherited from the base interface template,
  // even when abstract, to tell the compiler that all instances of the name
  // referred to by subclasses of this share the same implementation.
  cricket::MediaType media_type() const override = 0;
  void OnPacketReceived(rtc::CopyOnWriteBuffer packet,
                        int64_t packet_time_us) override = 0;
  void OnPacketSent(const rtc::SentPacket& sent_packet) override = 0;
  void OnReadyToSend(bool ready) override = 0;
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const rtc::NetworkRoute& network_route) override =
      0;

  // Sets the abstract interface class for sending RTP/RTCP data.
  virtual void SetInterface(MediaChannelNetworkInterface* iface);
  // Returns the absolute sendtime extension id value from media channel.
  virtual int GetRtpSendTimeExtnId() const;
  // Base method to send packet using MediaChannelNetworkInterface.
  bool SendPacket(rtc::CopyOnWriteBuffer* packet,
                  const rtc::PacketOptions& options);

  bool SendRtcp(rtc::CopyOnWriteBuffer* packet,
                const rtc::PacketOptions& options);

  int SetOption(MediaChannelNetworkInterface::SocketType type,
                rtc::Socket::Option opt,
                int option);

  // Corresponds to the SDP attribute extmap-allow-mixed, see RFC8285.
  // Set to true if it's allowed to mix one- and two-byte RTP header extensions
  // in the same stream. The setter and getter must only be called from
  // worker_thread.
  void SetExtmapAllowMixed(bool extmap_allow_mixed) override;
  bool ExtmapAllowMixed() const override;

  // Returns `true` if a non-null MediaChannelNetworkInterface pointer is held.
  // Must be called on the network thread.
  bool HasNetworkInterface() const;

  void SetFrameEncryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameEncryptorInterface>
                             frame_encryptor) override;
  void SetFrameDecryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameDecryptorInterface>
                             frame_decryptor) override;

  void SetEncoderToPacketizerFrameTransformer(
      uint32_t ssrc,
      rtc::scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override;
  void SetDepacketizerToDecoderFrameTransformer(
      uint32_t ssrc,
      rtc::scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override;

 protected:
  int SetOptionLocked(MediaChannelNetworkInterface::SocketType type,
                      rtc::Socket::Option opt,
                      int option) RTC_RUN_ON(network_thread_);

  bool DscpEnabled() const;

  // This is the DSCP value used for both RTP and RTCP channels if DSCP is
  // enabled. It can be changed at any time via `SetPreferredDscp`.
  rtc::DiffServCodePoint PreferredDscp() const;
  void SetPreferredDscp(rtc::DiffServCodePoint new_dscp);

  rtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> network_safety();

  // Utility implementation for derived classes (video/voice) that applies
  // the packet options and passes the data onwards to `SendPacket`.
  void SendRtp(const uint8_t* data,
               size_t len,
               const webrtc::PacketOptions& options);

  void SendRtcp(const uint8_t* data, size_t len);

 private:
  // Apply the preferred DSCP setting to the underlying network interface RTP
  // and RTCP channels. If DSCP is disabled, then apply the default DSCP value.
  void UpdateDscp() RTC_RUN_ON(network_thread_);

  bool DoSendPacket(rtc::CopyOnWriteBuffer* packet,
                    bool rtcp,
                    const rtc::PacketOptions& options);

  const bool enable_dscp_;
  const rtc::scoped_refptr<webrtc::PendingTaskSafetyFlag> network_safety_
      RTC_PT_GUARDED_BY(network_thread_);
  webrtc::TaskQueueBase* const network_thread_;
  MediaChannelNetworkInterface* network_interface_
      RTC_GUARDED_BY(network_thread_) = nullptr;
  rtc::DiffServCodePoint preferred_dscp_ RTC_GUARDED_BY(network_thread_) =
      rtc::DSCP_DEFAULT;
  bool extmap_allow_mixed_ = false;
};

// Base class for implementation classes

class VideoMediaChannel : public MediaChannel,
                          public VideoMediaSendChannelInterface,
                          public VideoMediaReceiveChannelInterface {
 public:
  explicit VideoMediaChannel(webrtc::TaskQueueBase* network_thread,
                             bool enable_dscp = false)
      : MediaChannel(network_thread, enable_dscp) {}
  ~VideoMediaChannel() override {}

  // Downcasting to the implemented interfaces.
  VideoMediaSendChannelInterface* AsVideoSendChannel() override { return this; }
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }

  VideoMediaReceiveChannelInterface* AsVideoReceiveChannel() override {
    return this;
  }
  cricket::MediaType media_type() const override;

  // Downcasting to the subclasses.
  VideoMediaChannel* AsVideoChannel() override { return this; }

  void SetExtmapAllowMixed(bool mixed) override {
    MediaChannel::SetExtmapAllowMixed(mixed);
  }
  bool ExtmapAllowMixed() const override {
    return MediaChannel::ExtmapAllowMixed();
  }
  // This fills the "bitrate parts" (rtx, video bitrate) of the
  // BandwidthEstimationInfo, since that part that isn't possible to get
  // through webrtc::Call::GetStats, as they are statistics of the send
  // streams.
  // TODO(holmer): We should change this so that either BWE graphs doesn't
  // need access to bitrates of the streams, or change the (RTC)StatsCollector
  // so that it's getting the send stream stats separately by calling
  // GetStats(), and merges with BandwidthEstimationInfo by itself.
  virtual void FillBitrateInfo(BandwidthEstimationInfo* bwe_info) = 0;
  // Gets quality stats for the channel.
  virtual bool GetStats(VideoMediaInfo* info) = 0;
  // Enable network condition based codec switching.
  void SetVideoCodecSwitchingEnabled(bool enabled) override;
};

// Base class for implementation classes
class VoiceMediaChannel : public MediaChannel,
                          public VoiceMediaSendChannelInterface,
                          public VoiceMediaReceiveChannelInterface {
 public:
  MediaType media_type() const override;
  VoiceMediaChannel(webrtc::TaskQueueBase* network_thread,
                    bool enable_dscp = false)
      : MediaChannel(network_thread, enable_dscp) {}
  ~VoiceMediaChannel() override {}

  // Downcasting to the implemented interfaces.
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override { return this; }

  VoiceMediaReceiveChannelInterface* AsVoiceReceiveChannel() override {
    return this;
  }

  VoiceMediaChannel* AsVoiceChannel() override { return this; }

  VideoMediaSendChannelInterface* AsVideoSendChannel() override {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }

  void SetExtmapAllowMixed(bool mixed) override {
    MediaChannel::SetExtmapAllowMixed(mixed);
  }
  bool ExtmapAllowMixed() const override {
    return MediaChannel::ExtmapAllowMixed();
  }

  // Gets quality stats for the channel.
  virtual bool GetStats(VoiceMediaInfo* info,
                        bool get_and_clear_legacy_stats) = 0;
};

// The externally exposed objects that support the Send and Receive interfaces.
// These dispatch their functions to the underlying MediaChannel objects.

class VoiceMediaSendChannel : public VoiceMediaSendChannelInterface {
 public:
  explicit VoiceMediaSendChannel(VoiceMediaChannel* impl) : impl_(impl) {}
  virtual ~VoiceMediaSendChannel() {}
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override { return this; }
  VideoMediaSendChannelInterface* AsVideoSendChannel() override {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }

  // Implementation of MediaBaseChannelInterface
  cricket::MediaType media_type() const override { return MEDIA_TYPE_AUDIO; }
  void OnPacketReceived(rtc::CopyOnWriteBuffer packet,
                        int64_t packet_time_us) override {
    impl()->OnPacketReceived(packet, packet_time_us);
  }
  void OnPacketSent(const rtc::SentPacket& sent_packet) override {
    impl()->OnPacketSent(sent_packet);
  }
  void OnReadyToSend(bool ready) override { impl()->OnReadyToSend(ready); }
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const rtc::NetworkRoute& network_route) override {
    impl()->OnNetworkRouteChanged(transport_name, network_route);
  }
  void SetExtmapAllowMixed(bool extmap_allow_mixed) override {
    impl()->SetExtmapAllowMixed(extmap_allow_mixed);
  }
  bool ExtmapAllowMixed() const override { return impl()->ExtmapAllowMixed(); }
  // Implementation of MediaSendChannelInterface
  bool AddSendStream(const StreamParams& sp) override {
    return impl()->AddSendStream(sp);
  }
  bool RemoveSendStream(uint32_t ssrc) override {
    return impl()->RemoveSendStream(ssrc);
  }
  void SetFrameEncryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameEncryptorInterface>
                             frame_encryptor) override {
    impl()->SetFrameEncryptor(ssrc, frame_encryptor);
  }
  webrtc::RTCError SetRtpSendParameters(
      uint32_t ssrc,
      const webrtc::RtpParameters& parameters,
      webrtc::SetParametersCallback callback = nullptr) override {
    return impl()->SetRtpSendParameters(ssrc, parameters, std::move(callback));
  }

  void SetEncoderToPacketizerFrameTransformer(
      uint32_t ssrc,
      rtc::scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override {
    return impl()->SetEncoderToPacketizerFrameTransformer(ssrc,
                                                          frame_transformer);
  }
  void SetEncoderSelector(uint32_t ssrc,
                          webrtc::VideoEncoderFactory::EncoderSelectorInterface*
                              encoder_selector) override {
    impl()->SetEncoderSelector(ssrc, encoder_selector);
  }
  webrtc::RtpParameters GetRtpSendParameters(uint32_t ssrc) const override {
    return impl()->GetRtpSendParameters(ssrc);
  }
  // Implementation of VoiceMediaSendChannel
  bool SetSendParameters(const AudioSendParameters& params) override {
    return impl()->SetSendParameters(params);
  }
  void SetSend(bool send) override { return impl()->SetSend(send); }
  bool SetAudioSend(uint32_t ssrc,
                    bool enable,
                    const AudioOptions* options,
                    AudioSource* source) override {
    return impl()->SetAudioSend(ssrc, enable, options, source);
  }
  bool CanInsertDtmf() override { return impl()->CanInsertDtmf(); }
  bool InsertDtmf(uint32_t ssrc, int event, int duration) override {
    return impl()->InsertDtmf(ssrc, event, duration);
  }

 private:
  VoiceMediaSendChannelInterface* impl() { return impl_; }
  const VoiceMediaSendChannelInterface* impl() const { return impl_; }
  VoiceMediaChannel* impl_;
};

class VoiceMediaReceiveChannel : public VoiceMediaReceiveChannelInterface {
 public:
  explicit VoiceMediaReceiveChannel(VoiceMediaChannel* impl) : impl_(impl) {}
  virtual ~VoiceMediaReceiveChannel() {}
  // Implementation of MediaBaseChannelInterface
  cricket::MediaType media_type() const override { return MEDIA_TYPE_AUDIO; }
  void OnPacketReceived(rtc::CopyOnWriteBuffer packet,
                        int64_t packet_time_us) override {
    impl()->OnPacketReceived(packet, packet_time_us);
  }
  void OnPacketSent(const rtc::SentPacket& sent_packet) override {
    impl()->OnPacketSent(sent_packet);
  }
  void OnReadyToSend(bool ready) override { impl()->OnReadyToSend(ready); }
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const rtc::NetworkRoute& network_route) override {
    impl()->OnNetworkRouteChanged(transport_name, network_route);
  }
  void SetExtmapAllowMixed(bool extmap_allow_mixed) override {
    impl()->SetExtmapAllowMixed(extmap_allow_mixed);
  }
  bool ExtmapAllowMixed() const override { return impl()->ExtmapAllowMixed(); }
  // Implementation of Delayable
  bool SetBaseMinimumPlayoutDelayMs(uint32_t ssrc, int delay_ms) override {
    return impl()->SetBaseMinimumPlayoutDelayMs(ssrc, delay_ms);
  }
  absl::optional<int> GetBaseMinimumPlayoutDelayMs(
      uint32_t ssrc) const override {
    return impl()->GetBaseMinimumPlayoutDelayMs(ssrc);
  }
  // Implementation of MediaReceiveChannelInterface
  bool AddRecvStream(const StreamParams& sp) override {
    return impl()->AddRecvStream(sp);
  }
  bool RemoveRecvStream(uint32_t ssrc) override {
    return impl()->RemoveRecvStream(ssrc);
  }
  void ResetUnsignaledRecvStream() override {
    return impl()->ResetUnsignaledRecvStream();
  }
  void OnDemuxerCriteriaUpdatePending() override {
    impl()->OnDemuxerCriteriaUpdatePending();
  }
  void OnDemuxerCriteriaUpdateComplete() override {
    impl()->OnDemuxerCriteriaUpdateComplete();
  }
  void SetFrameDecryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameDecryptorInterface>
                             frame_decryptor) override {
    impl()->SetFrameDecryptor(ssrc, frame_decryptor);
  }
  void SetDepacketizerToDecoderFrameTransformer(
      uint32_t ssrc,
      rtc::scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override {
    impl()->SetDepacketizerToDecoderFrameTransformer(ssrc, frame_transformer);
  }
  // Implementation of VoiceMediaReceiveChannelInterface
  bool SetRecvParameters(const AudioRecvParameters& params) override {
    return impl()->SetRecvParameters(params);
  }
  webrtc::RtpParameters GetRtpReceiveParameters(uint32_t ssrc) const override {
    return impl()->GetRtpReceiveParameters(ssrc);
  }
  std::vector<webrtc::RtpSource> GetSources(uint32_t ssrc) const override {
    return impl()->GetSources(ssrc);
  }
  webrtc::RtpParameters GetDefaultRtpReceiveParameters() const override {
    return impl()->GetDefaultRtpReceiveParameters();
  }
  void SetPlayout(bool playout) override { return impl()->SetPlayout(playout); }
  bool SetOutputVolume(uint32_t ssrc, double volume) override {
    return impl()->SetOutputVolume(ssrc, volume);
  }
  bool SetDefaultOutputVolume(double volume) override {
    return impl()->SetDefaultOutputVolume(volume);
  }
  void SetRawAudioSink(
      uint32_t ssrc,
      std::unique_ptr<webrtc::AudioSinkInterface> sink) override {
    return impl()->SetRawAudioSink(ssrc, std::move(sink));
  }
  void SetDefaultRawAudioSink(
      std::unique_ptr<webrtc::AudioSinkInterface> sink) override {
    return impl()->SetDefaultRawAudioSink(std::move(sink));
  }

 private:
  VoiceMediaReceiveChannelInterface* impl() { return impl_; }
  const VoiceMediaReceiveChannelInterface* impl() const { return impl_; }
  VoiceMediaChannel* impl_;
};

class VideoMediaSendChannel : public VideoMediaSendChannelInterface {
 public:
  explicit VideoMediaSendChannel(VideoMediaChannel* impl) : impl_(impl) {}

  VideoMediaSendChannelInterface* AsVideoSendChannel() override { return this; }
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }

  // Implementation of MediaBaseChannelInterface
  cricket::MediaType media_type() const override { return MEDIA_TYPE_AUDIO; }
  void OnPacketReceived(rtc::CopyOnWriteBuffer packet,
                        int64_t packet_time_us) override {
    impl()->OnPacketReceived(packet, packet_time_us);
  }
  void OnPacketSent(const rtc::SentPacket& sent_packet) override {
    impl()->OnPacketSent(sent_packet);
  }
  void OnReadyToSend(bool ready) override { impl()->OnReadyToSend(ready); }
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const rtc::NetworkRoute& network_route) override {
    impl()->OnNetworkRouteChanged(transport_name, network_route);
  }
  void SetExtmapAllowMixed(bool extmap_allow_mixed) override {
    impl()->SetExtmapAllowMixed(extmap_allow_mixed);
  }
  bool ExtmapAllowMixed() const override { return impl()->ExtmapAllowMixed(); }
  // Implementation of MediaSendChannelInterface
  bool AddSendStream(const StreamParams& sp) override {
    return impl()->AddSendStream(sp);
  }
  bool RemoveSendStream(uint32_t ssrc) override {
    return impl()->RemoveSendStream(ssrc);
  }
  void SetFrameEncryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameEncryptorInterface>
                             frame_encryptor) override {
    impl()->SetFrameEncryptor(ssrc, frame_encryptor);
  }
  webrtc::RTCError SetRtpSendParameters(
      uint32_t ssrc,
      const webrtc::RtpParameters& parameters,
      webrtc::SetParametersCallback callback = nullptr) override {
    return impl()->SetRtpSendParameters(ssrc, parameters, std::move(callback));
  }

  void SetEncoderToPacketizerFrameTransformer(
      uint32_t ssrc,
      rtc::scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override {
    return impl()->SetEncoderToPacketizerFrameTransformer(ssrc,
                                                          frame_transformer);
  }
  void SetEncoderSelector(uint32_t ssrc,
                          webrtc::VideoEncoderFactory::EncoderSelectorInterface*
                              encoder_selector) override {
    impl()->SetEncoderSelector(ssrc, encoder_selector);
  }
  webrtc::RtpParameters GetRtpSendParameters(uint32_t ssrc) const override {
    return impl()->GetRtpSendParameters(ssrc);
  }
  // Implementation of VideoMediaSendChannelInterface
  bool SetSendParameters(const VideoSendParameters& params) override {
    return impl()->SetSendParameters(params);
  }
  bool GetSendCodec(VideoCodec* send_codec) override {
    return impl()->GetSendCodec(send_codec);
  }
  bool SetSend(bool send) override { return impl()->SetSend(send); }
  bool SetVideoSend(
      uint32_t ssrc,
      const VideoOptions* options,
      rtc::VideoSourceInterface<webrtc::VideoFrame>* source) override {
    return impl()->SetVideoSend(ssrc, options, source);
  }
  void GenerateSendKeyFrame(uint32_t ssrc,
                            const std::vector<std::string>& rids) override {
    return impl()->GenerateSendKeyFrame(ssrc, rids);
  }
  void SetVideoCodecSwitchingEnabled(bool enabled) override {
    return impl()->SetVideoCodecSwitchingEnabled(enabled);
  }

 private:
  VideoMediaSendChannelInterface* impl() { return impl_; }
  const VideoMediaSendChannelInterface* impl() const { return impl_; }
  VideoMediaChannel* const impl_;
};

class VideoMediaReceiveChannel : public VideoMediaReceiveChannelInterface {
 public:
  explicit VideoMediaReceiveChannel(VideoMediaChannel* impl) : impl_(impl) {}
  // Implementation of MediaBaseChannelInterface
  cricket::MediaType media_type() const override { return MEDIA_TYPE_AUDIO; }
  void OnPacketReceived(rtc::CopyOnWriteBuffer packet,
                        int64_t packet_time_us) override {
    impl()->OnPacketReceived(packet, packet_time_us);
  }
  void OnPacketSent(const rtc::SentPacket& sent_packet) override {
    impl()->OnPacketSent(sent_packet);
  }
  void OnReadyToSend(bool ready) override { impl()->OnReadyToSend(ready); }
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const rtc::NetworkRoute& network_route) override {
    impl()->OnNetworkRouteChanged(transport_name, network_route);
  }
  void SetExtmapAllowMixed(bool extmap_allow_mixed) override {
    impl()->SetExtmapAllowMixed(extmap_allow_mixed);
  }
  bool ExtmapAllowMixed() const override { return impl()->ExtmapAllowMixed(); }
  // Implementation of Delayable
  bool SetBaseMinimumPlayoutDelayMs(uint32_t ssrc, int delay_ms) override {
    return impl()->SetBaseMinimumPlayoutDelayMs(ssrc, delay_ms);
  }
  absl::optional<int> GetBaseMinimumPlayoutDelayMs(
      uint32_t ssrc) const override {
    return impl()->GetBaseMinimumPlayoutDelayMs(ssrc);
  }
  // Implementation of MediaReceiveChannelInterface
  bool AddRecvStream(const StreamParams& sp) override {
    return impl()->AddRecvStream(sp);
  }
  bool RemoveRecvStream(uint32_t ssrc) override {
    return impl()->RemoveRecvStream(ssrc);
  }
  void ResetUnsignaledRecvStream() override {
    return impl()->ResetUnsignaledRecvStream();
  }
  void OnDemuxerCriteriaUpdatePending() override {
    impl()->OnDemuxerCriteriaUpdatePending();
  }
  void OnDemuxerCriteriaUpdateComplete() override {
    impl()->OnDemuxerCriteriaUpdateComplete();
  }
  void SetFrameDecryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameDecryptorInterface>
                             frame_decryptor) override {
    impl()->SetFrameDecryptor(ssrc, frame_decryptor);
  }
  void SetDepacketizerToDecoderFrameTransformer(
      uint32_t ssrc,
      rtc::scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override {
    impl()->SetDepacketizerToDecoderFrameTransformer(ssrc, frame_transformer);
  }
  // Implementation on videoMediaReceiveChannelInterface
  bool SetRecvParameters(const VideoRecvParameters& params) override {
    return impl()->SetRecvParameters(params);
  }
  webrtc::RtpParameters GetRtpReceiveParameters(uint32_t ssrc) const override {
    return impl()->GetRtpReceiveParameters(ssrc);
  }
  webrtc::RtpParameters GetDefaultRtpReceiveParameters() const override {
    return impl()->GetDefaultRtpReceiveParameters();
  }
  bool SetSink(uint32_t ssrc,
               rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override {
    return impl()->SetSink(ssrc, sink);
  }
  void SetDefaultSink(
      rtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override {
    return impl()->SetDefaultSink(sink);
  }
  void RequestRecvKeyFrame(uint32_t ssrc) override {
    return impl()->RequestRecvKeyFrame(ssrc);
  }
  std::vector<webrtc::RtpSource> GetSources(uint32_t ssrc) const override {
    return impl()->GetSources(ssrc);
  }
  // Set recordable encoded frame callback for `ssrc`
  void SetRecordableEncodedFrameCallback(
      uint32_t ssrc,
      std::function<void(const webrtc::RecordableEncodedFrame&)> callback)
      override {
    return impl()->SetRecordableEncodedFrameCallback(ssrc, std::move(callback));
  }
  // Clear recordable encoded frame callback for `ssrc`
  void ClearRecordableEncodedFrameCallback(uint32_t ssrc) override {
    impl()->ClearRecordableEncodedFrameCallback(ssrc);
  }

 private:
  VideoMediaReceiveChannelInterface* impl() { return impl_; }
  const VideoMediaReceiveChannelInterface* impl() const { return impl_; }
  VideoMediaChannel* const impl_;
};

}  // namespace cricket

#endif  // MEDIA_BASE_MEDIA_CHANNEL_IMPL_H_
