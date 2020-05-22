//
//  Vector2.swift
//  
//
//  Created by Miguel de Icaza on 5/15/20.
//

import Foundation
import CApiGodotSwift
public enum Axis3D {
    case x
    case y
    case z
}

extension Vector3 {
    static let zero = Vector3(x: 0, y: 0, z: 0)    

    subscript (axis: Axis3D) -> GFloat {
        get {
            switch axis {
            case .x:
                return x
            case .y:
                return y
            case .z:
                return z
            }
        }
        set {
            switch axis {
            case .x:
                x = newValue
            case .y:
                y = newValue
            case .z:
                z = newValue
            }
        }
    }
    
    func length () -> GFloat {
        return sqrt (x*x + y*y)
    }

    func lengthSquared () -> GFloat {
        return x*x + y*y + z*z
    }
    
    func normalized () -> Vector3
    {
        let lsq = lengthSquared()
        if lsq == 0.0 {
            return Vector3(x: 0, y: 0, z: 0)
        } else {
            let len = sqrt(lsq)
            return Vector3 (x: x / len, y: y / len, z: z/len)
        }
    }

    func cross (b: Vector3) -> Vector3 {
        return Vector3 (x: y * b.z - z * b.y,
                        y: z * b.x - x * b.z,
                        z: x * b.y - y * b.x)
    }
    
    func abs() -> Vector3 {
        return Vector3(x: x.magnitude, y: y.magnitude, z: z.magnitude)
    }
    
    static func + (lhs: Vector3, rhs: Vector3) -> Vector3
    {
        return Vector3(x: lhs.x+rhs.x, y: lhs.y+rhs.y, z: lhs.z+rhs.z)
    }

    static func - (lhs: Vector3, rhs: Vector3) -> Vector3
    {
        return Vector3(x: lhs.x-rhs.x, y: lhs.y-rhs.y, z:lhs.z-rhs.z)
    }
    
    static func almostEqual<T: FloatingPoint>(_ a: T, _ b: T) -> Bool {
        return a >= b.nextDown && a <= b.nextUp
    }
    
    static func < (lhs: Vector3, rhs: Vector3) -> Bool {
        if almostEqual(lhs.x, rhs.x) {
            if almostEqual(lhs.y, rhs.z) {
                return lhs.z < rhs.z
            } else {
                return lhs.y < rhs.y
            }
        }
        return lhs.x < rhs.x
    }
    
}



