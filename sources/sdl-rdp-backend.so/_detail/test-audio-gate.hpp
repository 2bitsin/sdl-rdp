#pragma once
#include "test-audio-session.hpp"

#include <future>
#include <memory>
#include <vector>

namespace BackendGate {
class AudioGate : public AudioSession {
protected:
  void         GivenConfirmingSession();
  void         ConnectAudioFormats(Client& client, SoundClient& audio);
  void         ThenInitialVolume(Client& client, SoundClient& audio);
  static void  ThenWriterFinishes(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                  unsigned frames);
  void         ThenFirstAudioBlockConfirms();
  void         WhenLastAudioBlockConfirms(std::vector<INT16> const& pcm);
  void         WhenIdleAudioBurst(std::vector<INT16> const& pcm, unsigned burst);
  static void  ThenDisconnectedWriter(Client& client, SoundClient& audio, std::future<int>& writing, bool reconnect,
                                      unsigned frames);
  void         ThenSlowAudioConfirms(std::future<int>& writing);
  static void  ThenInitialVolumeSamples(SoundClient const& audio);
  static void  ThenCapturedPcm(SoundClient const& audio, std::vector<INT16> const& pcm);
  static void  ThenMissingAudioHandle();
  void         GivenUnconfirmedSession();
  Client&      ClientSession();
  SoundClient& AudioSession();

private:
  std::unique_ptr<Client>      connected_client;
  std::unique_ptr<SoundClient> connected_audio;
};
}
