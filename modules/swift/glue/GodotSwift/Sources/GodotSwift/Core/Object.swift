//
// Object.swift
//  
// The base class for the GodotSwift hierarhcy
//
//  Created by Miguel de Icaza on 5/16/20.
//

import Foundation
import CApiGodotSwift

protocol GodotInstantiable {
    init (owns: Bool, handle: OpaquePointer?)
}

public class Object: CustomDebugStringConvertible, GodotInstantiable {
    var handle: OpaquePointer?
    var owns: Bool
    
    public init ()
    {
        self.owns = false
        self.handle = nil
        self.handle = OpaquePointer (godot_icall_Object_Ctor (OpaquePointer (Unmanaged.passRetained (self).toOpaque())))
    }
    
    public required init (owns: Bool, handle: OpaquePointer?)
    {
        self.owns = owns
        self.handle = handle
    }
    
    public var nativeInstance: OpaquePointer? {
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

    static var objectMap: [OpaquePointer:Any] = [:]

    static func lookupInstance<T:GodotInstantiable> (ptr: OpaquePointer) -> T
    {
        if let obj = objectMap [ptr] {
            return obj as! T
        }
        return T(owns: false, handle: ptr)
    }
}
