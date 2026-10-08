// MELODY LAB — analysis + generation engine (plain C++, no JUCE dependency)
#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace ml
{
// A sung note detected in the acapella. Times in seconds, pitch in (fractional) MIDI.
struct VocalNote { double startSec = 0, endSec = 0; float pitch = 0; };

// A generated / display note. Times in beats from bar 1 beat 1.
struct Note { double start = 0, length = 0; int pitch = 60; float velocity = 0.8f; };

struct ChordEvent { double start = 0, length = 0; std::string name; };

struct KeyResult
{
    int tonic = 0;  bool minor = false;  float confidence = 0.0f;
    int altTonic = 0; bool altMinor = false;
};

struct AnalysisResult
{
    std::vector<VocalNote> notes;
    double durationSec = 0.0;
    KeyResult key;
    bool valid = false;
};

enum class Style       { Pop = 0, RnB, Trap, House, Emotional, Afrobeats };
enum class MelodyMode  { Counter = 0, Hook, Harmony, Arp };
enum class HarmonyMode { Loop = 0, Follow };

struct GenSettings
{
    double bpm = 120.0;
    double offsetSec = 0.0;          // where bar 1 beat 1 sits in the acapella
    int  keyTonic = 0;  bool keyMinor = false;
    Style style = Style::Pop;
    MelodyMode  melodyMode  = MelodyMode::Counter;
    HarmonyMode harmonyMode = HarmonyMode::Loop;
    float density = 0.5f, complexity = 0.5f;
    int  octave = 0;                 // -1 .. +1
    int  barsPerChord = 1;           // 1 or 2
    uint32_t harmonySeed = 1, melodySeed = 1;
};

struct Generated
{
    std::vector<Note> melody, chords, bass;
    std::vector<Note> vocal;         // the detected vocal, in beats (for display)
    std::vector<ChordEvent> chordNames;
    double totalBeats = 16.0;
};

// Pitch-tracks a mono vocal and segments it into notes, then detects the key.
AnalysisResult analyse (const float* mono, int numSamples, double sampleRate);
KeyResult detectKey (const std::vector<VocalNote>& notes);

// Builds chords, bass and a melody that fit the analysed vocal.
Generated generate (const AnalysisResult& analysis, const GenSettings& settings);

std::string noteName (int pitchClass);
std::string keyName (int tonic, bool minor);
}
