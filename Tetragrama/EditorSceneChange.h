#pragma once
#include <cstdint>

namespace Tetragrama
{
    enum class SceneChangeKind : uint8_t
    {
        None,
        NewScene,
        OpenScene,
        CloseEditor,
    };

    enum class SceneChangeChoice : uint8_t
    {
        None,
        Save,
        Discard,
        Cancel,
    };

    enum class SceneChangeGate : uint8_t
    {
        Ready,
        Waiting,
        Save,
        Cancelled,
    };

    struct SceneChangePrompt
    {
        SceneChangeKind    Kind             = SceneChangeKind::None;
        uint64_t           Epoch            = 0;
        uint64_t           Revision         = 0;
        uint64_t           ExternalRevision = 0;
        uint64_t           Serial           = 0;
        bool               Saving           = false;
        bool               SaveFailed       = false;

        [[nodiscard]] bool Visible() const
        {
            return Kind != SceneChangeKind::None;
        }
    };

    /// Main-thread, allocation-free consent state. Does not change the document,
    /// its checkpoint or pending I/O. A discard authorizes one exact revision of
    /// the original scene, never a later edit or replacement world.
    class EditorSceneChange
    {
    public:
        [[nodiscard]] SceneChangeGate   Evaluate(SceneChangeKind kind, uint64_t epoch, uint64_t revision, bool dirty, uint64_t external_revision = 0);
        /// UI intent only. A stale/repeated response cannot approve a newer prompt.
        [[nodiscard]] bool              Choose(SceneChangeChoice choice, uint64_t prompt_serial);
        [[nodiscard]] bool              MayDiscard(SceneChangeKind kind, uint64_t epoch, uint64_t revision, uint64_t external_revision = 0) const;
        [[nodiscard]] SceneChangePrompt Prompt() const;
        void                            SaveFinished(uint64_t epoch, bool succeeded);
        void                            SaveCancelled(uint64_t epoch);
        void                            Finish(SceneChangeKind kind, uint64_t epoch);
        void                            Reset();

    private:
        enum class Phase : uint8_t
        {
            Idle,
            Confirm,
            Saving,
            Discarded,
            Cancelled,
        };

        void               Refresh(uint64_t revision, uint64_t external_revision, bool failed = false);
        [[nodiscard]] bool Matches(SceneChangeKind kind, uint64_t epoch) const;

        SceneChangePrompt  m_prompt      = {};
        SceneChangeChoice  m_choice      = SceneChangeChoice::None;
        Phase              m_phase       = Phase::Idle;
        uint64_t           m_next_serial = 1;
    };
} // namespace Tetragrama
