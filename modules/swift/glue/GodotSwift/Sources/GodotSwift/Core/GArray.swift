import Foundation

public class GArray {
    var ptr: OpaquePointer
    
    init (_ ptr: OpaquePointer)
    {
        self.ptr = ptr
    }
    
    func getHandle () -> OpaquePointer {
        return ptr
    }

}