#!/usr/bin/env python3
"""Write a small DAWproject file that uses every feature MusicDAWM imports.

The importer test (session_utils/dawproject_test.cc) checks the result of
importing it, so the two files must agree. Times in beats at 120 BPM
(1 beat = 0.5 s) until beat 32, where the tempo drops to 90 BPM and the time
signature becomes 3/4.

  Guitar  mono audio, -6 dB, pan 0.25, out to the "Guitar Bus" group;
          clip at beat 4, 8 beats long, starting 1 s into the file, 1-beat fade in;
          volume automation -6 dB at beat 0 -> -12 dB at beat 8
  Vocal   stereo audio at 48 kHz, gain 0.5, send 0.25 to "Reverb", a plug-in;
          Bitwig-style nested clip with warps at beat 8 (16 beats),
          a disabled clip at beat 40
  Room    muted; a 6-beat clip looping 2 s (4 beats) of audio
  Band    folder with "Bass": notes clip at beat 0, 8 beats, 4 notes (+1 outside)
  Reverb  effect channel; Guitar Bus: submix channel; Master: gain 0.8
  Markers "Verse" at beat 0, "Chorus" at beat 16

Usage: make_test_project.py <out.dawproject>
"""
import io
import sys
import wave
import zipfile

import numpy as np


def wav_bytes(seconds: float, rate: int, channels: int, freq: float) -> bytes:
    t = np.arange(int(seconds * rate)) / rate
    x = (0.2 * np.sin(2 * np.pi * freq * t) * 32767).astype("<i2")
    if channels == 2:
        x = np.stack([x, x], axis=1).reshape(-1)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(x.tobytes())
    return buf.getvalue()


PROJECT = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Project version="1.0">
  <Application name="MusicDAWM test" version="1"/>
  <Transport>
    <Tempo unit="bpm" value="120.0" id="t0" name="Tempo"/>
    <TimeSignature numerator="4" denominator="4" id="t1"/>
  </Transport>
  <Structure>
    <Track contentType="audio" loaded="true" id="trG" name="Guitar" color="#ff0000">
      <Channel audioChannels="1" destination="chGB" role="regular" solo="false" id="chG">
        <Mute value="false" id="gMute" name="Mute"/>
        <Pan max="1.0" min="0.0" unit="normalized" value="0.25" id="gPan" name="Pan"/>
        <Volume unit="decibel" value="-6.0" id="gVol" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio" loaded="true" id="trV" name="Vocal">
      <Channel audioChannels="2" destination="chM" role="regular" solo="false" id="chV">
        <Devices>
          <Vst3Plugin deviceID="ABCDEF" deviceName="Pro-Q 3" deviceRole="audioFX" loaded="true" id="dev1" name="Pro-Q 3">
            <State path="plugins/proq.vstpreset"/>
          </Vst3Plugin>
        </Devices>
        <Mute value="false" id="vMute" name="Mute"/>
        <Pan max="1.0" min="0.0" unit="normalized" value="0.5" id="vPan" name="Pan"/>
        <Sends>
          <Send destination="chR" type="post" id="vSend">
            <Volume max="2.0" min="0.0" unit="linear" value="0.25" id="vSendVol" name="Send"/>
          </Send>
        </Sends>
        <Volume max="2.0" min="0.0" unit="linear" value="0.5" id="vVol" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio" loaded="true" id="trR" name="Room">
      <Channel audioChannels="1" destination="chM" role="regular" id="chRo">
        <Mute value="true" id="rMute" name="Mute"/>
        <Volume unit="linear" value="1.0" id="rVol" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="tracks" loaded="true" id="trF" name="Band">
      <Track contentType="notes" loaded="true" id="trB" name="Bass">
        <Channel audioChannels="2" destination="chM" role="regular" id="chB">
          <Volume unit="linear" value="1.0" id="bVol" name="Volume"/>
        </Channel>
      </Track>
    </Track>
    <Track contentType="audio" loaded="true" id="trRev" name="Reverb">
      <Channel audioChannels="2" destination="chM" role="effect" id="chR">
        <Volume unit="linear" value="1.0" id="revVol" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio" loaded="true" id="trGB" name="Guitar Bus">
      <Channel audioChannels="2" destination="chM" role="submix" id="chGB">
        <Volume unit="linear" value="1.0" id="gbVol" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio notes" loaded="true" id="trM" name="Master">
      <Channel audioChannels="2" role="master" id="chM">
        <Volume max="2.0" min="0.0" unit="linear" value="0.8" id="mVol" name="Volume"/>
      </Channel>
    </Track>
  </Structure>
  <Arrangement id="arr">
    <Lanes timeUnit="beats" id="l0">
      <Lanes track="trG" id="lG">
        <Clips id="cG">
          <Clip time="4.0" duration="8.0" contentTimeUnit="seconds" playStart="1.0" fadeTimeUnit="beats" fadeInTime="1.0" fadeOutTime="0.0" name="Guitar take">
            <Audio channels="1" duration="20.0" sampleRate="44100" timeUnit="seconds" id="aG">
              <File path="audio/guitar.wav"/>
            </Audio>
          </Clip>
        </Clips>
        <Points id="pG" unit="decibel">
          <Target parameter="gVol"/>
          <RealPoint time="0.0" value="-6.0" interpolation="linear"/>
          <RealPoint time="8.0" value="-12.0" interpolation="linear"/>
        </Points>
      </Lanes>
      <Lanes track="trV" id="lV">
        <Clips id="cV">
          <Clip time="8.0" duration="16.0" playStart="0.0" name="Vocal take">
            <Clips id="cV2">
              <Clip time="0.0" duration="16.0" contentTimeUnit="beats" playStart="0.0">
                <Warps contentTimeUnit="seconds" timeUnit="beats" id="wV">
                  <Audio channels="2" duration="30.0" sampleRate="48000" id="aV">
                    <File path="audio/vocal.wav"/>
                  </Audio>
                  <Warp time="0.0" contentTime="0.0"/>
                  <Warp time="16.0" contentTime="8.0"/>
                </Warps>
              </Clip>
            </Clips>
          </Clip>
          <Clip time="40.0" duration="4.0" contentTimeUnit="seconds" playStart="0.0" enable="false" name="Spare">
            <Audio channels="2" duration="30.0" sampleRate="48000" id="aV2">
              <File path="audio/vocal.wav"/>
            </Audio>
          </Clip>
        </Clips>
      </Lanes>
      <Lanes track="trR" id="lR">
        <Clips id="cR">
          <Clip time="0.0" duration="6.0" contentTimeUnit="seconds" playStart="0.0" loopStart="0.0" loopEnd="2.0" name="Room loop">
            <Audio channels="1" duration="20.0" sampleRate="44100" id="aR">
              <File path="audio/room.wav"/>
            </Audio>
          </Clip>
        </Clips>
      </Lanes>
      <Lanes track="trB" id="lB">
        <Clips id="cB">
          <Clip time="0.0" duration="8.0" playStart="0.0" name="Bass line">
            <Notes id="nB">
              <Note time="0.0" duration="1.0" channel="0" key="40" vel="0.8" rel="0.5"/>
              <Note time="2.0" duration="1.0" channel="0" key="43" vel="0.8" rel="0.5"/>
              <Note time="4.0" duration="1.0" channel="0" key="45" vel="0.8" rel="0.5"/>
              <Note time="6.0" duration="2.0" channel="0" key="47" vel="1.0" rel="0.5"/>
              <Note time="9.0" duration="1.0" channel="0" key="50" vel="0.8" rel="0.5"/>
            </Notes>
          </Clip>
        </Clips>
      </Lanes>
    </Lanes>
    <Markers id="mk">
      <Marker time="0.0" name="Verse"/>
      <Marker time="16.0" name="Chorus"/>
    </Markers>
    <TempoAutomation id="ta" unit="bpm">
      <Target parameter="t0"/>
      <RealPoint time="0.0" value="120.0" interpolation="hold"/>
      <RealPoint time="32.0" value="90.0" interpolation="hold"/>
    </TempoAutomation>
    <TimeSignatureAutomation id="tsa">
      <Target parameter="t1"/>
      <TimeSignaturePoint time="32.0" numerator="3" denominator="4"/>
    </TimeSignatureAutomation>
  </Arrangement>
  <Scenes/>
</Project>
"""

METADATA = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<MetaData><Title>DAWproject import test</Title></MetaData>
"""


def main() -> None:
    out = sys.argv[1]
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("project.xml", PROJECT)
        z.writestr("metadata.xml", METADATA)
        z.writestr("audio/guitar.wav", wav_bytes(20, 44100, 1, 220))
        z.writestr("audio/vocal.wav", wav_bytes(30, 48000, 2, 330))
        z.writestr("audio/room.wav", wav_bytes(20, 44100, 1, 110))
        z.writestr("plugins/proq.vstpreset", b"not a real preset")
        # an archive entry that tries to escape the folder must be refused
        z.writestr("../escape.txt", b"must not be written")
    print(out)


if __name__ == "__main__":
    main()
