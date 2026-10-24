#include "ui/blob_window.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "[TEST] Testing BlobWindow::create()...\n";

    screenmate::ui::BlobWindow blob(128);
    bool ok = blob.create(200, 200);
    if (!ok) {
        std::cerr << "[FAIL] blob.create() returned false! GetLastError: " << GetLastError() << "\n";
        return 1;
    }

    std::cout << "[PASS] blob.create() succeeded! HWND: " << blob.hwnd() << "\n";

    blob.update_frame(0.0);
    blob.show();
    blob.close();

    std::cout << "[PASS] BlobWindow lifecycle verified cleanly.\n";
    return 0;
}
