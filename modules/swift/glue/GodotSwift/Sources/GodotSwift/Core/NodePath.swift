import Foundation

public class NodePath {
    var handle: OpaquePointer
    
    init (_ handle: OpaquePointer)
    {
        self.handle = handle
    }

    static func getHandle (_ nodePath: NodePath?) -> OpaquePointer?
    {
            if let x = nodePath {
                    return x.handle
            }
            return nil
    }
}