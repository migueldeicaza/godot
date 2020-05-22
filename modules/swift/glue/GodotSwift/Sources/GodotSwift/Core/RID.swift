import Foundation

public class RID {
        var handle: OpaquePointer

        init (_ handle: OpaquePointer)
        {
                self.handle = handle
        }

        public init (from: Object)
        {
                abort()
        }

        public func getId () -> Int32 
        {
                abort()
        }
}