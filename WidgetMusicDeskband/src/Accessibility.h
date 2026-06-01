#pragma once

#include <oleacc.h>

#include <atomic>
#include <string>

namespace widgetmusic {

inline constexpr long kAccessiblePrevious = 1;
inline constexpr long kAccessiblePlayPause = 2;
inline constexpr long kAccessibleNext = 3;
inline constexpr long kAccessibleButtonCount = 3;

class AccessibleHost {
 public:
  virtual HWND AccessibleWindow() const = 0;
  virtual bool AccessibleButtonEnabled(long childId) const = 0;
  virtual std::wstring AccessibleButtonName(long childId) const = 0;
  virtual RECT AccessibleButtonScreenRect(long childId) const = 0;
  virtual long AccessibleFocusedButton() const = 0;
  virtual void AccessibleFocusButton(long childId) = 0;
  virtual bool AccessibleInvokeButton(long childId) = 0;

 protected:
  ~AccessibleHost() = default;
};

class AccessibleButtons final : public IAccessible {
 public:
  explicit AccessibleButtons(AccessibleHost* host) : _host(host) {}

  void Detach() { _host = nullptr; }

  IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
      *ppv = static_cast<IAccessible*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  IFACEMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(_ref.fetch_add(1) + 1); }
  IFACEMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = static_cast<ULONG>(_ref.fetch_sub(1) - 1);
    if (remaining == 0) delete this;
    return remaining;
  }

  IFACEMETHODIMP GetTypeInfoCount(UINT* pctinfo) override {
    if (!pctinfo) return E_POINTER;
    *pctinfo = 0;
    return S_OK;
  }
  IFACEMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
  IFACEMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
  IFACEMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override {
    return E_NOTIMPL;
  }

  IFACEMETHODIMP get_accParent(IDispatch** parent) override {
    if (!parent) return E_POINTER;
    *parent = nullptr;
    const HWND hwnd = Window();
    const HWND parentWindow = hwnd ? ::GetParent(hwnd) : nullptr;
    return parentWindow
               ? ::AccessibleObjectFromWindow(parentWindow, OBJID_WINDOW, IID_IDispatch,
                                              reinterpret_cast<void**>(parent))
               : S_FALSE;
  }

  IFACEMETHODIMP get_accChildCount(long* count) override {
    if (!count) return E_POINTER;
    *count = kAccessibleButtonCount;
    return S_OK;
  }

  IFACEMETHODIMP get_accChild(VARIANT, IDispatch** child) override {
    if (!child) return E_POINTER;
    *child = nullptr;
    return S_FALSE;
  }

  IFACEMETHODIMP get_accName(VARIANT child, BSTR* name) override {
    if (!name) return E_POINTER;
    *name = nullptr;
    std::wstring value;
    if (IsSelf(child)) {
      value = L"Widget Music";
    } else {
      const long childId = ChildId(child);
      if (!ValidChild(childId) || !_host) return E_INVALIDARG;
      value = _host->AccessibleButtonName(childId);
    }
    *name = ::SysAllocString(value.c_str());
    return *name ? S_OK : E_OUTOFMEMORY;
  }

  IFACEMETHODIMP get_accValue(VARIANT, BSTR* value) override {
    if (!value) return E_POINTER;
    *value = nullptr;
    return S_FALSE;
  }

  IFACEMETHODIMP get_accDescription(VARIANT child, BSTR* description) override {
    if (!description) return E_POINTER;
    *description = nullptr;
    const wchar_t* value = IsSelf(child) ? L"Taskbar media controls" : L"Media control button";
    *description = ::SysAllocString(value);
    return *description ? S_OK : E_OUTOFMEMORY;
  }

  IFACEMETHODIMP get_accRole(VARIANT child, VARIANT* role) override {
    if (!role) return E_POINTER;
    ::VariantInit(role);
    if (!IsSelf(child) && !ValidChild(ChildId(child))) return E_INVALIDARG;
    role->vt = VT_I4;
    role->lVal = IsSelf(child) ? ROLE_SYSTEM_TOOLBAR : ROLE_SYSTEM_PUSHBUTTON;
    return S_OK;
  }

  IFACEMETHODIMP get_accState(VARIANT child, VARIANT* state) override {
    if (!state) return E_POINTER;
    ::VariantInit(state);
    state->vt = VT_I4;
    if (IsSelf(child)) {
      state->lVal = STATE_SYSTEM_FOCUSABLE;
      if (Window() && ::GetFocus() == Window()) state->lVal |= STATE_SYSTEM_FOCUSED;
      return S_OK;
    }
    const long childId = ChildId(child);
    if (!ValidChild(childId) || !_host) return E_INVALIDARG;
    state->lVal = STATE_SYSTEM_FOCUSABLE;
    if (!_host->AccessibleButtonEnabled(childId)) state->lVal |= STATE_SYSTEM_UNAVAILABLE;
    if (::GetFocus() == Window() && _host->AccessibleFocusedButton() == childId) {
      state->lVal |= STATE_SYSTEM_FOCUSED;
    }
    return S_OK;
  }

  IFACEMETHODIMP get_accHelp(VARIANT, BSTR* help) override {
    if (!help) return E_POINTER;
    *help = nullptr;
    return S_FALSE;
  }

  IFACEMETHODIMP get_accHelpTopic(BSTR* helpFile, VARIANT, long* topicId) override {
    if (!helpFile || !topicId) return E_POINTER;
    *helpFile = nullptr;
    *topicId = -1;
    return S_FALSE;
  }

  IFACEMETHODIMP get_accKeyboardShortcut(VARIANT, BSTR* shortcut) override {
    if (!shortcut) return E_POINTER;
    *shortcut = nullptr;
    return S_FALSE;
  }

  IFACEMETHODIMP get_accFocus(VARIANT* focused) override {
    if (!focused) return E_POINTER;
    ::VariantInit(focused);
    if (!_host || ::GetFocus() != Window()) return S_OK;
    focused->vt = VT_I4;
    focused->lVal = _host->AccessibleFocusedButton();
    return S_OK;
  }

  IFACEMETHODIMP get_accSelection(VARIANT* selected) override {
    if (!selected) return E_POINTER;
    ::VariantInit(selected);
    return S_OK;
  }

  IFACEMETHODIMP get_accDefaultAction(VARIANT child, BSTR* action) override {
    if (!action) return E_POINTER;
    *action = nullptr;
    if (IsSelf(child)) return S_FALSE;
    if (!ValidChild(ChildId(child))) return E_INVALIDARG;
    *action = ::SysAllocString(L"Press");
    return *action ? S_OK : E_OUTOFMEMORY;
  }

  IFACEMETHODIMP accSelect(long flags, VARIANT child) override {
    if (!_host) return E_FAIL;
    const long childId = ChildId(child);
    if (!ValidChild(childId)) return E_INVALIDARG;
    if ((flags & (SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION)) != 0) {
      _host->AccessibleFocusButton(childId);
      return S_OK;
    }
    return S_FALSE;
  }

  IFACEMETHODIMP accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
    if (!left || !top || !width || !height) return E_POINTER;
    RECT rc{};
    if (IsSelf(child)) {
      if (!Window() || !::GetWindowRect(Window(), &rc)) return E_FAIL;
    } else {
      const long childId = ChildId(child);
      if (!ValidChild(childId) || !_host) return E_INVALIDARG;
      rc = _host->AccessibleButtonScreenRect(childId);
    }
    *left = rc.left;
    *top = rc.top;
    *width = rc.right - rc.left;
    *height = rc.bottom - rc.top;
    return S_OK;
  }

  IFACEMETHODIMP accNavigate(long direction, VARIANT from, VARIANT* destination) override {
    if (!destination) return E_POINTER;
    ::VariantInit(destination);
    long childId = ChildId(from);
    if (IsSelf(from)) {
      if (direction == NAVDIR_FIRSTCHILD) childId = kAccessiblePrevious;
      else if (direction == NAVDIR_LASTCHILD) childId = kAccessibleNext;
      else return S_FALSE;
    } else if (direction == NAVDIR_NEXT && ValidChild(childId) && childId < kAccessibleNext) {
      ++childId;
    } else if (direction == NAVDIR_PREVIOUS && ValidChild(childId) && childId > kAccessiblePrevious) {
      --childId;
    } else {
      return S_FALSE;
    }
    destination->vt = VT_I4;
    destination->lVal = childId;
    return S_OK;
  }

  IFACEMETHODIMP accHitTest(long x, long y, VARIANT* child) override {
    if (!child) return E_POINTER;
    ::VariantInit(child);
    if (!_host) return S_FALSE;
    POINT point{x, y};
    for (long childId = kAccessiblePrevious; childId <= kAccessibleNext; ++childId) {
      const RECT rc = _host->AccessibleButtonScreenRect(childId);
      if (::PtInRect(&rc, point)) {
        child->vt = VT_I4;
        child->lVal = childId;
        return S_OK;
      }
    }
    child->vt = VT_I4;
    child->lVal = CHILDID_SELF;
    return S_OK;
  }

  IFACEMETHODIMP accDoDefaultAction(VARIANT child) override {
    if (!_host) return E_FAIL;
    const long childId = ChildId(child);
    if (!ValidChild(childId)) return E_INVALIDARG;
    return _host->AccessibleInvokeButton(childId) ? S_OK : S_FALSE;
  }

  IFACEMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
  IFACEMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

 private:
  static bool IsSelf(const VARIANT& child) { return child.vt == VT_I4 && child.lVal == CHILDID_SELF; }
  static long ChildId(const VARIANT& child) { return child.vt == VT_I4 ? child.lVal : -1; }
  static bool ValidChild(long childId) {
    return childId >= kAccessiblePrevious && childId <= kAccessibleNext;
  }

  HWND Window() const { return _host ? _host->AccessibleWindow() : nullptr; }

  std::atomic<long> _ref{1};
  AccessibleHost* _host = nullptr;
};

}  // namespace widgetmusic
