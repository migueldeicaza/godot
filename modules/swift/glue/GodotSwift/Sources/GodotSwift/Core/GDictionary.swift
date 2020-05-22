public class GDictionary {
        var ptr: OpaquePointer
        init (_ ptr: OpaquePointer)
        {
                self.ptr = ptr
        }

        static func getHandle (_ d: GDictionary?) -> OpaquePointer?
        {
                if let x = d {
                        return x.ptr
                }
                return nil
        }

        func getHandle () -> OpaquePointer
        {
                return ptr
        }

}
