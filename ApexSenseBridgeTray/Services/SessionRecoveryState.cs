namespace ApexSenseBridgeTray.Services
{
    // In-memory only: restarting Tray must never resume a previous game silently.
    public sealed class SessionRecoveryState
    {
        public string Game { get; private set; }
        public string Profile { get; private set; }
        public int ApexSlot { get; private set; }
        public string Owner { get; private set; }
        public bool Pending { get; private set; }
        public bool BlocksAutomaticActivation { get; private set; }
        public bool Resuming { get; private set; }
        public bool Recovered { get; private set; }
        public uint Stages { get; private set; }
        public bool ControllerAvailable { get; private set; }

        public bool Offer(string game, string profile, int slot, uint interruption, uint stages, string owner = "Tray")
        {
            // Only a confirmed runtime interruption offers recovery, not a startup
            // refusal, deliberate stop or generic crash. Begin() separately prevents
            // taking over an external owner's session.
            if ((interruption < 1 || interruption > 3) || (stages & 8) == 0) return false;
            Game = game; Profile = profile; ApexSlot = slot;
            Owner = owner;
            Pending = true; BlocksAutomaticActivation = true;
            Resuming = false; Recovered = false; Stages = 0;
            ControllerAvailable = false;
            return true;
        }

        public bool Begin()
        {
            if (!Pending || Resuming || Owner != "Tray") return false;
            Resuming = true; Stages = 0; Recovered = false;
            return true;
        }

        public void Observe(uint stages) { if (Resuming) Stages |= stages & 15; }
        public void ControllerDetected(bool available) { ControllerAvailable = available; }
        public void Complete(bool success)
        {
            Resuming = false;
            Recovered = success;
            Pending = !success;
            BlocksAutomaticActivation = !success;
        }
        public void Dismiss()
        {
            if (Resuming) return;
            Pending = false; Recovered = false;
            // Keep automatic detection suspended until this game closes or the
            // user explicitly starts a fresh manual session.
        }
        public void Clear()
        {
            Game = null; Profile = null; ApexSlot = 0;
            Owner = null;
            Pending = false; BlocksAutomaticActivation = false;
            Resuming = false; Recovered = false; Stages = 0;
            ControllerAvailable = false;
        }
        public SessionRecoveryState Snapshot() { return (SessionRecoveryState)MemberwiseClone(); }
    }
}
