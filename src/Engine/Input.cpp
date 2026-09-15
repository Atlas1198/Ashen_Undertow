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
    m_attackHeld = false;
    m_attackReleased = false;
    m_blockHeld = false;
    m_jumpPressed = false;
    m_colliderVisualizationPressed = false;
    m_specialAttackPressed = false;
    m_menuLeftPressed = false;
    m_menuRightPressed = false;
    m_menuUpPressed = false;
    m_menuDownPressed = false;
    m_confirmPressed = false;
    const bool focused = m_window && GetForegroundWindow() == m_window;
    const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
    const bool leftDown = down(VK_LBUTTON);
    const bool rightDown = down(VK_RBUTTON);
    const bool spaceDown = down(VK_SPACE);
    const bool cDown = down('C');
    const bool bothMouseDown = leftDown && rightDown;
    const bool escapeDown = down(VK_ESCAPE);
    const bool leftArrowDown = down(VK_LEFT);
    const bool rightArrowDown = down(VK_RIGHT);
    const bool upArrowDown = down(VK_UP);
    const bool downArrowDown = down(VK_DOWN);
    const bool wDown = down('W');
    const bool sDown = down('S');
    const bool enterDown = down(VK_RETURN);
    if (!focused || escapeDown) m_controlEnabled = false;

    POINT mouse {};
    RECT rect {};
    const bool hasMouse = GetCursorPos(&mouse) != FALSE;
    const bool hasRect = m_window && GetClientRect(m_window, &rect);
    POINT client = mouse;
    m_attackPressed = focused && m_controlEnabled && leftDown && !rightDown &&
        !m_leftWasDown;
    m_attackHeld = focused && m_controlEnabled && leftDown && !rightDown;
    m_attackReleased = focused && m_controlEnabled &&
        (!leftDown || rightDown) && m_leftWasDown;
    m_blockHeld = focused && m_controlEnabled && rightDown && !leftDown;
    if (focused && !escapeDown && leftDown && !m_leftWasDown && hasMouse && hasRect &&
        ScreenToClient(m_window, &client) && PtInRect(&rect, client))
        m_controlEnabled = true;
    m_leftWasDown = leftDown;
    m_jumpPressed = focused && spaceDown && !m_spaceWasDown;
    m_spaceWasDown = spaceDown;
    m_colliderVisualizationPressed = focused && cDown && !m_cWasDown;
    m_cWasDown = cDown;
    m_specialAttackPressed = focused && m_controlEnabled && bothMouseDown &&
        !m_bothMouseWasDown;
    m_bothMouseWasDown = bothMouseDown;
    m_menuLeftPressed = focused && leftArrowDown && !m_leftArrowWasDown;
    m_menuRightPressed = focused && rightArrowDown && !m_rightArrowWasDown;
    m_menuUpPressed = focused && ((upArrowDown && !m_upArrowWasDown) ||
        (wDown && !m_wWasDown));
    m_menuDownPressed = focused && ((downArrowDown && !m_downArrowWasDown) ||
        (sDown && !m_sWasDown));
    m_confirmPressed = focused && enterDown && !m_enterWasDown;
    m_leftArrowWasDown = leftArrowDown;
    m_rightArrowWasDown = rightArrowDown;
    m_upArrowWasDown = upArrowDown;
    m_downArrowWasDown = downArrowDown;
    m_wWasDown = wDown;
    m_sWasDown = sDown;
    m_enterWasDown = enterDown;

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
