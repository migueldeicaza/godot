//
// Object.swift
//  
// The base class for the GodotSwift hierarhcy
//
//  Created by Miguel de Icaza on 5/16/20.
//

import Foundation

public class Object: CustomDebugStringConvertible {
    var handle: OpaquePointer
    var owns: Bool
    
    public convenience init ()
    {
        self.init (owns: false, handle: godot_call_object_ctor ())
    }
    
    public init (owns: Bool, handle: OpaquePointer)
    {
        self.owns = owns
        self.handle = handle
    }
    
    public var nativeInstance: OpaquePointer {
        get {
            handle
        }
    }
    
    var debugDescription: String {
        get {
            return "Godot can provide this1"
        }
    }
}
