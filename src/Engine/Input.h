#pragma once

#include <Windows.h>

// Win32入力とカーソルの所有者。Managerが毎フレーム一度だけ更新する。
class Input
{
public:
    Input() = default;
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    void Initialize(HWND window) { m_window = window; }
    void Update();
    float MoveX() const { return m_moveX; }
    float MoveZ() const { return m_moveZ; }
    bool DashHeld() const { return m_dashHeld; }
    bool AttackPressed() const { return m_attackPressed; }
    float LookX() const { return m_lookX; }
    float LookY() const { return m_lookY; }

private:
    void RestoreCursor();
    HWND m_window = nullptr;
    POINT m_lastMouse {};
    bool m_controlEnabled = true;
    bool m_haveMouseSample = false;
    bool m_leftWasDown = false;
    bool m_dashHeld = false;
    bool m_attackPressed = false;
    int m_hideCalls = 0;
    float m_moveX = 0, m_moveZ = 0, m_lookX = 0, m_lookY = 0;
};
