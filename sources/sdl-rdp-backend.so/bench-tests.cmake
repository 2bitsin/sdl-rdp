# CTest appends LABELS in TEST_INCLUDE_FILES; configure-time set_property cannot see discovered tests.
set_tests_properties(AudioGate.AudioPlaybackConfirmsKeepRealtimeStreamContinuous
                  AudioGate.AudioContinuousUnderProgressiveLoad
                  AudioGate.AudioNeverConfirmsUsesServerClock
                  RoundFive.NeverAcknowledgesClock
                  GraphicsCost.FullRandomFrame GraphicsCost.AvcFullFrame
             PROPERTIES LABELS bench)
