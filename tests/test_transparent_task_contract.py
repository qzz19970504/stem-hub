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


def test_uart1_preserves_idle_chunk_metadata() -> None:
    runtime_header = read_source("App/Inc/app_runtime.h")
    runtime_source = read_source("App/Src/app_runtime.c")

    assert "uint8_t uart1_rx_chunk[APP_UART1_RX_CHUNK_SIZE];" in runtime_header
    assert "App_RuntimePopUart1Chunk" in runtime_header
    assert "App_RuntimeConsumeUart1Overflow" in runtime_header
    assert "HAL_UARTEx_ReceiveToIdle_IT(&huart1" in runtime_source
    assert "HAL_UARTEx_RxEventCallback" in runtime_source
    assert "AppUartChunkQueue_Push" in runtime_source
    assert "HAL_UART_Receive_IT(&huart1" not in runtime_source
    assert "App_RuntimePopUart1Byte" not in runtime_header


def test_at_task_routes_at_and_transparent_data_exclusively() -> None:
    at_task_source = read_source("App/Src/app_at_task.c")

    assert "AppTransparentMode" in at_task_source
    assert "AppLineReader" in at_task_source
    assert "APP_AT_COMMAND_START_TRANSPARENT" in at_task_source
    assert "App_RuntimePopUart1Chunk" in at_task_source
    assert "AppTransparentMode_ProcessChunk" in at_task_source
    assert "App_RuntimeClearBridgeTarget" in at_task_source
    assert "static AppTransparentMode transparent_mode;" in at_task_source
    assert "static uint8_t chunk[APP_UART1_RX_CHUNK_SIZE];" in at_task_source
    assert "App_AtForwardLine" not in at_task_source
    assert "App_RuntimePopUart1Byte" not in at_task_source


def test_transparent_support_modules_are_linked() -> None:
    cmake_source = read_source("cmake/stm32cubemx/CMakeLists.txt")

    assert "App/Src/app_line_reader.c" in cmake_source
    assert "App/Src/app_transparent_mode.c" in cmake_source
    assert "App/Src/app_uart_chunk_queue.c" in cmake_source
