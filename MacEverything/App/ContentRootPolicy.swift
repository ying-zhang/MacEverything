enum ContentRootPolicy {
    static func runtimeRoots(useMainIndexRoots: Bool,
                             indexRoots: [String],
                             customRoots: [String]) -> [String] {
        useMainIndexRoots ? indexRoots : customRoots
    }
}
