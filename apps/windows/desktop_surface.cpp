#include "desktop_surface.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QScreen>
#include <cstring>
#include <windows.h>
#include <windowsx.h>

namespace {
BOOL CALLBACK findDesktop(HWND window, LPARAM value) {
  if (auto view = FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr)) {
    DWORD pid = 0, explorer = 0;
    GetWindowThreadProcessId(window, &pid);
    GetWindowThreadProcessId(GetShellWindow(), &explorer);
    if (pid && pid == explorer) {
      // Modern Explorer composes the layered DefView, not arbitrary siblings
      // under its WS_EX_NOREDIRECTIONBITMAP Progman root.
      *reinterpret_cast<HWND *>(value) = view;
      return FALSE;
    }
  }
  return TRUE;
}
struct MonitorPosition {
  QString name;
  QPoint origin;
};
BOOL CALLBACK findMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM value) {
  auto &position = *reinterpret_cast<MonitorPosition *>(value);
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info) && position.name == QString::fromWCharArray(info.szDevice)) {
    position.origin = {info.rcMonitor.left, info.rcMonitor.top};
    return FALSE;
  }
  return TRUE;
}
QPointF logicalPoint(const DesktopSurface *surface, QPointF local) {
  for (auto *screen : QGuiApplication::screens())
    if (screen->name() == surface->desktopScreen())
      return QPointF(screen->geometry().topLeft() + surface->desktopOffset()) + local;
  return QPointF(surface->desktopOffset()) + local;
}
LRESULT CALLBACK surfaceProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  return DesktopSurface::dispatchNative(reinterpret_cast<quintptr>(window), message, wparam,
                                        lparam);
}
ATOM surfaceClass() {
  static const ATOM atom = [] {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = surfaceProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"AllpowersDesktopSurface.v1";
    return RegisterClassExW(&cls);
  }();
  return atom;
}
} // namespace
DesktopSurface::DesktopSurface(bool enabled, std::function<quintptr()> finder)
    : enabled_(enabled), hostFinder_(std::move(finder)) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
  connect(&retry, &QTimer::timeout, this, &DesktopSurface::attach);
  connect(qApp, &QGuiApplication::screenRemoved, this, [this] { attach(); });
  connect(qApp, &QGuiApplication::screenAdded, this, [this] { attach(); });
  if (enabled_)
    retry.start(1500);
}
DesktopSurface::~DesktopSurface() {
  retry.stop();
  if (handle) {
    auto window = reinterpret_cast<HWND>(handle);
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    DestroyWindow(window);
  }
}
qreal DesktopSurface::desktopScale() const {
  for (auto *screen : QGuiApplication::screens())
    if (screen->name() == screenName_)
      return screen->devicePixelRatio();
  auto *screen = QGuiApplication::primaryScreen();
  return screen ? screen->devicePixelRatio() : 1.;
}
void DesktopSurface::setDesktopPosition(QString screen, QPoint offset) {
  screenName_ = std::move(screen);
  offset_ = offset;
  attach();
}
void DesktopSurface::attach() {
  if (!enabled_)
    return;
  auto *screen = QGuiApplication::primaryScreen();
  if (!screen)
    return;
  for (auto *candidate : QGuiApplication::screens())
    if (candidate->name() == screenName_)
      screen = candidate;
  const bool oldAttached = attached();
  const auto oldError = error_;
  screenName_ = screen->name();
  const QRect available = screen->availableGeometry().translated(-screen->geometry().topLeft());
  offset_.setX(qBound(available.left(), offset_.x(),
                      qMax(available.left(), available.right() - width() + 1)));
  offset_.setY(qBound(available.top(), offset_.y(),
                      qMax(available.top(), available.bottom() - height() + 1)));
  HWND parent = nullptr;
  if (hostFinder_)
    parent = reinterpret_cast<HWND>(hostFinder_());
  else
    EnumWindows(findDesktop, reinterpret_cast<LPARAM>(&parent));
  if (handle &&
      (!IsWindow(reinterpret_cast<HWND>(handle)) || shell != reinterpret_cast<quintptr>(parent))) {
    if (IsWindow(reinterpret_cast<HWND>(handle)))
      DestroyWindow(reinterpret_cast<HWND>(handle));
    handle = 0;
    shell = 0;
    published_ = false;
  }
  bool changed = false;
  if (!parent) {
    error_ = QStringLiteral("Рабочий стол Explorer недоступен. Повторяем подключение…");
  } else {
    if (!handle && surfaceClass()) {
      const auto previous = SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(parent));
      HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                    L"AllpowersDesktopSurface.v1", L"ALLPOWERS Widget",
                                    WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, parent, nullptr,
                                    GetModuleHandleW(nullptr), this);
      const DWORD error = window ? 0 : GetLastError();
      if (previous)
        SetThreadDpiAwarenessContext(previous);
      handle = reinterpret_cast<quintptr>(window);
      shell = window ? reinterpret_cast<quintptr>(parent) : 0;
      changed = true;
      if (!window)
        error_ = QStringLiteral("Не удалось создать карточку: %1").arg(error);
    }
    if (handle) {
      auto window = reinterpret_cast<HWND>(handle);
      MonitorPosition position{screenName_, screen->geometry().topLeft()};
      EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&position));
      const qreal scale = desktopScale();
      POINT point{position.origin.x() + qRound(offset_.x() * scale),
                  position.origin.y() + qRound(offset_.y() * scale)};
      ScreenToClient(parent, &point);
      RECT rect{};
      GetWindowRect(window, &rect);
      POINT origin{rect.left, rect.top};
      ScreenToClient(parent, &origin);
      const int w = qRound(width() * scale), h = qRound(height() * scale);
      changed = changed || origin.x != point.x || origin.y != point.y ||
                rect.right - rect.left != w || rect.bottom - rect.top != h;
      if (changed) {
        SetWindowPos(window, HWND_TOP, point.x, point.y, w, h, SWP_NOACTIVATE);
        error_.clear();
        emit surfaceChanged();
        publish();
      }
    }
  }
  if (oldAttached != attached() || oldError != error_)
    emit attachmentChanged();
}
void DesktopSurface::presentImage(const QImage &image) {
  frame_ = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  publish();
}
void DesktopSurface::publish() {
  if (!handle || frame_.isNull())
    return;
  HWND window = reinterpret_cast<HWND>(handle);
  if (!IsWindow(window))
    return;
  BITMAPINFO bitmap{};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = frame_.width();
  bitmap.bmiHeader.biHeight = -frame_.height();
  bitmap.bmiHeader.biPlanes = 1;
  bitmap.bmiHeader.biBitCount = 32;
  bitmap.bmiHeader.biCompression = BI_RGB;
  void *pixels = nullptr;
  HDC memory = CreateCompatibleDC(nullptr);
  HBITMAP dib = CreateDIBSection(memory, &bitmap, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (!memory || !dib || !pixels) {
    if (dib)
      DeleteObject(dib);
    if (memory)
      DeleteDC(memory);
    error_ = QStringLiteral("Не удалось выделить изображение карточки");
    return;
  }
  std::memcpy(pixels, frame_.constBits(), size_t(frame_.sizeInBytes()));
  const auto previous = SelectObject(memory, dib);
  // Bitmap publication must not move a child window. Feeding screen coordinates
  // to its parent-relative destination accumulates the host offset every frame.
  POINT source{0, 0};
  SIZE size{frame_.width(), frame_.height()};
  BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  const BOOL ok =
      UpdateLayeredWindow(window, nullptr, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA);
  const DWORD error = ok ? 0 : GetLastError();
  SelectObject(memory, previous);
  DeleteObject(dib);
  DeleteDC(memory);
  published_ = bool(ok);
  if (ok && !IsWindowVisible(window))
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (!ok)
    error_ = QStringLiteral("Не удалось отрисовать карточку: %1").arg(error);
}
void DesktopSurface::capturePointer() {
  if (handle)
    SetCapture(reinterpret_cast<HWND>(handle));
  else if (!enabled_)
    grabMouse();
}
void DesktopSurface::releasePointer() {
  if (handle && GetCapture() == reinterpret_cast<HWND>(handle))
    ReleaseCapture();
  else if (!enabled_)
    releaseMouse();
}
qintptr DesktopSurface::dispatchNative(quintptr id, quint32 message, quintptr wparam,
                                       qintptr lparam) {
  auto window = reinterpret_cast<HWND>(id);
  if (message == WM_NCCREATE) {
    auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto *self = reinterpret_cast<DesktopSurface *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (self) {
    switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT:
      ValidateRect(window, nullptr);
      return 0;
    case WM_SETCURSOR:
      SetCursor(LoadCursorW(nullptr,
                            self->cursor().shape() == Qt::SizeAllCursor ? IDC_SIZEALL : IDC_ARROW));
      return TRUE;
    case WM_CAPTURECHANGED: {
      QEvent lost(QEvent::UngrabMouse);
      QCoreApplication::sendEvent(self, &lost);
      return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE: {
      if (QApplication::activeModalWidget())
        return 0;
      POINT global{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const QPointF local(global.x / self->desktopScale(), global.y / self->desktopScale());
      const auto type = message == WM_LBUTTONDOWN ? QEvent::MouseButtonPress
                        : message == WM_LBUTTONUP ? QEvent::MouseButtonRelease
                                                  : QEvent::MouseMove;
      const auto button = message == WM_MOUSEMOVE ? Qt::NoButton : Qt::LeftButton;
      const Qt::MouseButtons buttons = (wparam & MK_LBUTTON) ? Qt::LeftButton : Qt::NoButton;
      QMouseEvent event(type, local, logicalPoint(self, local), button, buttons, Qt::NoModifier);
      QCoreApplication::sendEvent(self, &event);
      return 0;
    }
    case WM_CONTEXTMENU: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(window, &point);
      const QPointF local(point.x / self->desktopScale(), point.y / self->desktopScale());
      QContextMenuEvent event(QContextMenuEvent::Mouse, local.toPoint(),
                              logicalPoint(self, local).toPoint());
      QCoreApplication::sendEvent(self, &event);
      return 0;
    }
    case WM_NCDESTROY:
      self->handle = 0;
      self->shell = 0;
      self->published_ = false;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      break;
    }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
QJsonObject DesktopSurface::nativeDiagnostics() const {
  auto window = reinterpret_cast<HWND>(handle);
  const bool valid = window && IsWindow(window);
  const auto style = valid ? GetWindowLongPtrW(window, GWL_STYLE) : 0;
  return {{"native_visible", valid && bool(IsWindowVisible(window))},
          {"native_child", valid && bool(style & WS_CHILD)},
          {"native_parent_matches", valid && GetParent(window) == reinterpret_cast<HWND>(shell)},
          {"native_popup", valid && bool(style & WS_POPUP)},
          {"native_frame_published", published_}};
}
