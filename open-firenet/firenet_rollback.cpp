// The core marks a new image valid as soon as it starts. Defer that, so that open-firenet.ino confirms it only once
// the bridge is on Wi-Fi and running (otherwise the previous image comes back at the next restart).
// Plain C linkage is needed to replace the core's weak function, hence this separate file.
extern "C" bool verifyRollbackLater() { return true; }
