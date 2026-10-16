#ifndef BROWSER_UI_BLYNX_COMMAND_CENTER_H_
#define BROWSER_UI_BLYNX_COMMAND_CENTER_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "ui/gfx/native_ui_types.h"

class BrowserWindowInterface;
namespace views {
class Widget;
}  // namespace views

namespace browser {

// Window-owned: searches only this window, including in private windows.
class BlynxCommandCenter {
 public:
  BlynxCommandCenter(BrowserWindowInterface& browser,
                     gfx::NativeWindow context,
                     gfx::NativeView parent);
  ~BlynxCommandCenter();
  bool IsClosed() const;
  void Show();

 private:
  class Delegate;
  void Close();
  std::unique_ptr<Delegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
};

}  // namespace browser
#endif  // BROWSER_UI_BLYNX_COMMAND_CENTER_H_
