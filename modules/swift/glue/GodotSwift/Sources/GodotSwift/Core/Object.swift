//
// Object.swift
//  
// The base class for the GodotSwift hierarhcy
//
//  Created by Miguel de Icaza on 5/16/20.
//

import Foundation
import CApiGodotSwift

class SubclassTest: Object {
    public override init ()
    {
        super.init (owns: false, handle: nil)
        self.handle = Unmanaged.passRetained (self).toOpaque()
    }
}

public class Object: CustomDebugStringConvertible {
    var handle: UnsafeMutableRawPointer?
    var owns: Bool
    
    public init ()
    {
        self.owns = false
        self.handle = nil
        self.handle = godot_icall_Object_Ctor (OpaquePointer (Unmanaged.passRetained (self).toOpaque()))
    }
    
    public init (owns: Bool, handle: UnsafeMutableRawPointer?)
    {
        self.owns = owns
        self.handle = handle
    }
    
    public var nativeInstance: UnsafeMutableRawPointer? {
        get {
            handle
        }
    }
    
    public var debugDescription: String {
        get {
            return "Godot can provide this1"
        }
    }

    static func classDBgetMethod (type: String, method: String) -> OpaquePointer
    {
        print ("Calling Object.classDBgetMethod -- should call the bridge code")
        abort ();
    }
}
