from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_source(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def test_bridge_selection_and_drain_are_serialized() -> None:
    runtime_header = read_source("App/Inc/app_runtime.h")
    runtime_source = read_source("App/Src/app_runtime.c")
    bridge_source = read_source("App/Src/app_bridge_task.c")

    assert "osMutexId_t bridge_mutex;" in runtime_header
    assert "App_RuntimeLockBridge" in runtime_header
    assert "App_RuntimeUnlockBridge" in runtime_header
    assert "App_RuntimeSelectBridgeTarget" in runtime_header
    assert "App_RuntimeClearBridgeTarget" in runtime_header
    assert "g_app_runtime.bridge_mutex = osMutexNew" in runtime_source
    assert "App_StateSelectBridgeTarget" in runtime_source
    assert "App_StateClearBridgeTarget" in runtime_source
    assert "App_RuntimeLockBridge();" in bridge_source
    assert "App_RuntimeUnlockBridge();" in bridge_source
