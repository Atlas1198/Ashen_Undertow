#include "Input.h"

Input::~Input() { RestoreCursor(); }

void Input::RestoreCursor()
{
    while (m_hideCalls > 0)
    {
        ShowCursor(TRUE);
        --m_hideCalls;
    }
}

void Input::Update()
{
    m_moveX = m_moveZ = m_lookX = m_lookY = 0;
    m_dashHeld = false;
    m_attackPressed = false;
    const bool focused = m_window && GetForegroundWindow() == m_window;
    const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    const bool leftDown = down(VK_LBUTTON);
    const bool escapeDown = down(VK_ESCAPE);
    if (!focused || escapeDown) m_controlEnabled = false;

    POINT mouse {};
    RECT rect {};
    const bool hasMouse = GetCursorPos(&mouse) != FALSE;
    const bool hasRect = m_window && GetClientRect(m_window, &rect);
    POINT client = mouse;
    m_attackPressed = focused && m_controlEnabled && leftDown && !m_leftWasDown;
    if (focused && !escapeDown && leftDown && !m_leftWasDown && hasMouse && hasRect &&
        ScreenToClient(m_window, &client) && PtInRect(&rect, client))
        m_controlEnabled = true;
    m_leftWasDown = leftDown;

    if (focused)
    {
        m_moveX = float(down('D')) - float(down('A'));
        m_moveZ = float(down('W')) - float(down('S'));
        m_dashHeld = down(VK_SHIFT);
    }
    if (!focused || !m_controlEnabled || !hasMouse || !hasRect)
    {
        RestoreCursor();
        m_haveMouseSample = false;
        return;
    }
    if (m_hideCalls == 0)
    {
        int count;
        do { count = ShowCursor(FALSE); ++m_hideCalls; } while (count >= 0);
    }
    if (m_haveMouseSample)
    {
        m_lookX = float(mouse.x - m_lastMouse.x);
        m_lookY = float(mouse.y - m_lastMouse.y);
    }
    m_haveMouseSample = true;
    m_lastMouse = mouse;
    POINT center { (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 };
    if (ClientToScreen(m_window, &center) && SetCursorPos(center.x, center.y))
        m_lastMouse = center;
}
