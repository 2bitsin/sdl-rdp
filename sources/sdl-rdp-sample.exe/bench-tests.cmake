# CTest appends LABELS in TEST_INCLUDE_FILES; configure-time set_property cannot see discovered tests.
set_tests_properties(Sample.InputAndClipboardUnderTightVideo AudioDriver.NoClientTenSecondClock
                  AudioSample.BlockCadence AudioDriver.LeadCadence
                  AudioDriver.StallRefillClock AudioDriver.InitialLeadClock
                  AudioDriver.ZeroLeadKeepsRealtimeClock
                  VideoDriver.DefaultPresentDoesNotWaitForAcknowledgements
             PROPERTIES LABELS bench)
