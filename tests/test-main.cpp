#include "test-framework.h"

#include <exception>

int main() {
    Test::Context context;
    try {
        RunResultTests(context);
        RunEventTests(context);
        RunCryptoTests(context);
        RunUrlTests(context);
        RunJsonTests(context);
        RunOptionTests(context);
        RunEmbeddedWebTests(context);
        RunProtocolTests(context);
        RunBridgeServerTests(context);
    } catch (const std::exception& exception) {
        context.Check(false, std::string("Unexpected exception: ") + exception.what());
    } catch (...) {
        context.Check(false, "Unexpected unknown exception");
    }
    return context.Finish();
}
