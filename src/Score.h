#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
namespace music {
    using Tick = std::int64_t;
    using EventId = std::uint64_t;
    using StaffId = std::uint64_t;
    using PartId = std::uint64_t;
    using GroupId = std::uint64_t;
    using TupletId = std::uint64_t;
    // Exact for non-nested 3/5/7 tuplets, including dotted written values.
    constexpr Tick quarter = 6720;
    constexpr Tick measureTicks = 4 * quarter;
    // default meter, not the document time grid
    struct Pitch {
        // C++ and worker use [step (C=0 .. B=6), scientific octave (C4=MIDI 60, C5=MIDI 72), semitone alteration].
        int step = 0;
        int octave = 4;
        int alter = 0;
        [[nodiscard]] int midi() const;
        bool operator==(const Pitch&) const = default;
    };
    struct Event {
        EventId id = 0;
        Tick ticks = 0;
        std::vector<Pitch> pitches;
        std::vector<EventId> ties;
        // outgoing target per pitch; empty means no ties
        TupletId tuplet = 0;
        Tick written = 0;
        [[nodiscard]] Tick writtenDuration() const {
            return tuplet ? written : ticks;
        }
        [[nodiscard]] bool isRest() const {
            return pitches.empty();
        }
        [[nodiscard]] EventId tieTarget(std::size_t tone) const {
            return tone < ties.size() ? ties[tone] : 0;
        }
        bool operator==(const Event&) const = default;
    };
    struct Tuplet {
        TupletId id = 0;
        Tick start = 0;
        int actual = 3, normal = 2;
        Tick unit = quarter / 2;
        Tick length = 0;
        // Zero retains the full group span used by existing scores.
        [[nodiscard]] Tick span() const { return length ? length : unit * normal; }
        [[nodiscard]] Tick soundingDuration(Tick written) const {
            return actual > 0 && normal > 0 && written > 0 && written <= 6*quarter &&
                written * normal % actual == 0 ? written * normal / actual : 0;
        }
        [[nodiscard]] bool valid() const;
        bool operator==(const Tuplet&) const = default;
    };
    struct Measure {
        std::vector<Event> events;
        std::vector<Tuplet> tuplets{};
        bool operator==(const Measure&) const = default;
    };
    struct Meter {
        int numerator = 4;
        int denominator = 4;
        std::vector<int> groups {
            1, 1, 1, 1
        };
        [[nodiscard]] Tick ticks() const {
            return quarter * 4 * numerator / denominator;
        }
        [[nodiscard]] bool valid() const;
        bool operator==(const Meter&) const = default;
    };
    struct TempoMark {
        Tick beat = quarter;
        int bpm = 120;
        [[nodiscard]] bool valid() const;
        bool operator==(const TempoMark&) const = default;
    };
    struct Bar {
        Meter meter;
        bool meterChange = false;
        std::optional<TempoMark> tempo;
        bool operator==(const Bar&) const = default;
    };
    enum class Clef {
        Treble, Bass, Alto, Tenor, Treble8va, Bass8vb
    };
    struct ClefChange {
        Tick tick = 0;
        Clef clef = Clef::Treble;
        bool operator==(const ClefChange&) const = default;
    };
    enum class ScaleIdentity { Major, Minor };
    enum class MinorForm { Melodic, Harmonic, Aeolian };
    struct Tonality {
        int step=0,alter=0;
        ScaleIdentity scale=ScaleIdentity::Major;
        MinorForm minor=MinorForm::Melodic;
        bool operator==(const Tonality&) const = default;
    };
    struct KeyContext {
        // Absence means undeclared, rather than an assertion of C major.
        std::optional<int> fifths=0;
        std::optional<Tonality> tonality=Tonality{};
        [[nodiscard]] bool valid() const;
        [[nodiscard]] int alteration(int step) const;
        [[nodiscard]] std::string label() const;
        [[nodiscard]] static KeyContext fromSignature(int fifths,ScaleIdentity scale,MinorForm form=MinorForm::Melodic);
        bool operator==(const KeyContext&) const = default;
    };
    struct KeyChange {
        Tick tick=0;
        KeyContext context;
        bool operator==(const KeyChange&) const = default;
    };
    struct Staff {
        StaffId id = 0;
        PartId part = 0;
        std::string name;
        int program = -1;
        // inherits the part's program
        std::vector<Measure> measures;
        std::vector<ClefChange> clefs {
            {
                0, Clef::Treble
            }
        };
        std::vector<KeyChange> keys{{0,{}}};
        bool operator==(const Staff&) const = default;
    };
    struct Part {
        PartId id = 0;
        std::string name = "Piano";
        int program = 0;
        bool operator==(const Part&) const = default;
    };
    struct StaffGroup {
        GroupId id = 0;
        std::string name;
        std::vector<PartId> parts;
        bool operator==(const StaffGroup&) const = default;
    };
    struct Location {
        std::size_t measure = 0;
        std::size_t event = 0;
        std::size_t staff = 0;
    };
    struct BarBundle {
        Bar bar;
        std::vector<Measure> measures;
    };
    struct ScoreRange {
        Tick start=0,end=0;
        std::vector<StaffId> staves;
    };
    struct FragmentEvent {
        Tick ticks=0;
        std::vector<Pitch> pitches;
        std::vector<bool> ties;
        int actual=1,normal=1;
        Tick unit=0;
        TupletId group=0;
    };
    struct ScoreFragment {
        Tick duration=0;
        std::vector<std::vector<FragmentEvent>> rows;
        [[nodiscard]] bool valid() const;
    };
    // Normalize exact proposed timing into clipboard-compatible notation values.
    [[nodiscard]] std::optional<ScoreFragment> normalizeFragment(const ScoreFragment& fragment,std::string* error=nullptr);
    struct MeasureEdit {
        std::size_t staff = 0;
        std::size_t measure = 0;
        Measure before;
        Measure after;
    };
    struct LocalEdit {
        std::vector<MeasureEdit> measures;
        std::vector<BarBundle> appended;
    };
    struct KeyEdit {
        std::size_t staff=0;
        std::vector<KeyChange> before,after;
        std::vector<std::size_t> measures;
    };
    struct EditImpact {
        std::vector<std::size_t> measures;
        bool reset=false;
    };
    class Score {
        public:
        explicit Score(std::size_t measures = 4);
        [[nodiscard]] std::size_t measureCount() const {
            return bars_.size();
        }
        [[nodiscard]] std::size_t staffCount() const {
            return staves_.size();
        }
        [[nodiscard]] const Measure& measure(std::size_t index, std::size_t staff = 0) const {
            return staves_.at(staff).measures.at(index);
        }
        [[nodiscard]] const Bar& bar(std::size_t index) const {
            return bars_.at(index);
        }
        [[nodiscard]] const std::vector<Bar>& bars() const {
            return bars_;
        }
        [[nodiscard]] const std::vector<Staff>& staves() const {
            return staves_;
        }
        [[nodiscard]] const std::vector<Part>& parts() const {
            return parts_;
        }
        [[nodiscard]] const std::vector<StaffGroup>& groups() const {
            return groups_;
        }
        [[nodiscard]] Tick measureStart(std::size_t index) const {
            return starts_.at(index);
        }
        [[nodiscard]] Tick totalTicks() const {
            return starts_.back();
        }
        [[nodiscard]] std::size_t measureAt(Tick tick) const;
        [[nodiscard]] Tick eventStart(EventId id) const;
        [[nodiscard]] std::optional<Location> locate(EventId id) const;
        [[nodiscard]] const Event* event(EventId id) const;
        [[nodiscard]] EventId eventAt(std::size_t staff,Tick tick) const;
        [[nodiscard]] const Tuplet* tupletFor(EventId id) const;
        [[nodiscard]] EventId firstEventId(std::size_t measureIndex, std::size_t staff = 0) const;
        [[nodiscard]] EventId nextEventId(EventId id) const;
        [[nodiscard]] EventId previousEventId(EventId id) const;
        [[nodiscard]] Clef clefAt(std::size_t staff, Tick tick) const;
        [[nodiscard]] const KeyContext& keyAt(std::size_t staff,Tick tick) const;
        [[nodiscard]] int entryAlteration(std::size_t staff,Tick tick,int step,int octave) const;
        std::optional<KeyEdit> setKey(std::size_t staff,Tick tick,KeyContext key);
        std::optional<KeyEdit> removeKey(std::size_t staff,Tick tick);
        bool restoreKeys(std::size_t staff,const std::vector<KeyChange>& keys);
        [[nodiscard]] TempoMark tempoAt(std::size_t measure) const;
        [[nodiscard]] int program(std::size_t staff) const;
        [[nodiscard]] std::optional<std::size_t> staffIndex(StaffId id) const;
        bool enterNote(EventId restId, Pitch pitch, Tick ticks);
        bool addChordPitch(EventId id, Pitch pitch, std::size_t* newIndex = nullptr);
        bool setPitch(EventId id, std::size_t pitchIndex, Pitch pitch);
        bool setDuration(EventId id, Tick ticks, bool overwrite = false, bool fit = false);
        bool createTuplet(EventId id, int count);
        [[nodiscard]] std::optional<ScoreFragment> copyRange(const ScoreRange& range) const;
        std::optional<LocalEdit> pasteRange(const ScoreFragment& fragment,const ScoreRange& destination,std::string* error=nullptr);
        bool makeRest(EventId id);
        bool toggleTie(EventId id, std::size_t tone);
        bool setClef(std::size_t staff, Tick tick, Clef clef);
        bool removeClef(std::size_t staff, Tick tick);
        bool setTempo(std::size_t measure, std::optional<TempoMark> tempo);
        bool changeMeter(std::size_t measure, Meter meter);
        PartId addPart(std::string name, int program, bool piano = false);
        StaffId addStaff(PartId part, Clef clef = Clef::Treble);
        bool removeStaff(StaffId id);
        bool moveStaff(StaffId id, int direction);
        bool setInstrument(StaffId staff, int program, std::string name);
        bool setPartInstrument(PartId part, int program, std::string name);
        bool addGroup(std::string name, std::vector<PartId> parts);
        bool removeGroup(GroupId id);
        EventId appendMeasure(std::size_t staff = 0);
        bool removeLastMeasure();
        bool restoreMeasure(std::size_t index, const Measure& measure, std::size_t staff = 0);
        bool restoreAppendedMeasure(const Measure& measure);
        bool restoreBundle(const BarBundle& bundle);
        [[nodiscard]] BarBundle bundle(std::size_t measure) const;
        bool loadMeasures(std::vector<Measure> measures);
        bool loadDocument(std::vector<Bar> bars, std::vector<Staff> staves, std::vector<Part> parts, std::vector<StaffGroup> groups = {});
        [[nodiscard]] bool valid() const;
        [[nodiscard]] static bool validMeasure(const Measure& measure, Tick length = measureTicks);
        [[nodiscard]] static bool validDuration(Tick ticks);
        [[nodiscard]] static Tick baseDuration(Tick ticks);
        [[nodiscard]] static Tick largestDuration(Tick ticks);
        [[nodiscard]] static Pitch nearestPitch(int step, const Pitch& anchor);
        [[nodiscard]] static Meter defaultMeter(int numerator, int denominator);
        private:
        Event* mutableEvent(EventId id);
        void reindexMeasure(std::size_t staff, std::size_t measure);
        void rebuildIndex();
        void rebuildStarts();
        void cleanTiesNear(EventId id);
        std::vector<Event> makeRests(Tick ticks, const Tuplet* tuplet = nullptr);
        EventId nextId_ = 1;
        std::uint64_t nextObject_ = 3;
        std::vector<Bar> bars_;
        std::vector<Tick> starts_;
        std::vector<Staff> staves_;
        std::vector<Part> parts_;
        std::vector<StaffGroup> groups_;
        std::unordered_map<EventId, Location> index_;
        std::vector<std::vector<std::vector<EventId>>> indexedIds_;
    };
    struct EditChange {
        std::size_t measureIndex = 0;
        Measure before;
        Measure after;
        std::optional<Measure> appended;
    };
    struct StructuralEdit {
        std::unique_ptr<Score> before;
        std::unique_ptr<Score> after;
    };
    class EditHistory {
        public:
        void record(EditChange change);
        void record(LocalEdit change);
        void record(KeyEdit change);
        void record(Score before, const Score& after);
        bool undo(Score& score, EditImpact* impact = nullptr);
        bool redo(Score& score, EditImpact* impact = nullptr);
        void clear();
        [[nodiscard]] bool canUndo() const {
            return !undo_.empty();
        }
        [[nodiscard]] bool canRedo() const {
            return !redo_.empty();
        }
        private:
        using Change = std::variant<LocalEdit, StructuralEdit,KeyEdit>;
        bool apply(Score& score, const Change& change, bool forward, EditImpact* impact);
        std::vector<Change> undo_;
        std::vector<Change> redo_;
    };
}
// namespace music
