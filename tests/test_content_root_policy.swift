import Foundation

private var failures = 0

private func expect(_ condition: Bool, _ message: String) {
    if !condition {
        failures += 1
        print("FAIL: \(message)")
    }
}

@main
struct ContentRootPolicyTests {
    static func main() {
        let mainRoots = ["/main/a", "/main/b"]
        let customRoots = ["/custom/text"]

        expect(ContentRootPolicy.runtimeRoots(
            useMainIndexRoots: true, indexRoots: mainRoots, customRoots: customRoots
        ) == mainRoots, "enabled switch uses main index roots")
        expect(ContentRootPolicy.runtimeRoots(
            useMainIndexRoots: false, indexRoots: mainRoots, customRoots: customRoots
        ) == customRoots, "disabled switch uses custom content roots")
        expect(ContentRootPolicy.runtimeRoots(
            useMainIndexRoots: false, indexRoots: mainRoots, customRoots: []
        ).isEmpty, "empty custom roots remain empty")

        if failures > 0 { exit(1) }
        print("content root policy tests passed")
    }
}
