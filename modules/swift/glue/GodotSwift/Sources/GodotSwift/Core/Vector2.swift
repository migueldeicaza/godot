//
//  Vector2.swift
//  
//
//  Created by Miguel de Icaza on 5/15/20.
//

import Foundation

public enum Axis2D {
    case x
    case y
}
public struct Vector2: Equatable {
    public var x: GFloat
    public var y: GFloat
    
    public init (x: GFloat, y: GFloat)
    {
        self.x = x
        self.y = y
    }
    
    public subscript (axis: Axis2D) -> GFloat {
        get {
            switch axis {
            case .x:
                return x
            case .y:
                return y
            }
        }
        set {
            switch axis {
            case .x:
                x = newValue
            case .y:
                y = newValue
            }
        }
    }
    
    public func length () -> GFloat {
        return sqrt (x*x + y*y)
    }

    public func lengthSquared () -> GFloat {
        return x*x + y*y
    }
    
    public func normalized () -> Vector2
    {
        let lsq = lengthSquared()
        if lsq == 0.0 {
            return Vector2(x: 0, y: 0)
        } else {
            let len = sqrt(lsq)
            return Vector2 (x: x / len, y: y / len)
        }
    }

    public func cross (b: Vector2) -> GFloat {
        return x * b.y + y * b.x
    }
    
    public func abs() -> Vector2 {
        return Vector2(x: x.magnitude, y: y.magnitude)
    }
    
    static public func + (lhs: Vector2, rhs: Vector2) -> Vector2
    {
        return Vector2(x: lhs.x+rhs.x, y: lhs.y+rhs.y)
    }

    static public func - (lhs: Vector2, rhs: Vector2) -> Vector2
    {
        return Vector2(x: lhs.x-rhs.x, y: lhs.y-rhs.y)
    }
    
    static func almostEqual<T: FloatingPoint>(_ a: T, _ b: T) -> Bool {
        return a >= b.nextDown && a <= b.nextUp
    }
    
    public static func < (lhs: Vector2, rhs: Vector2) -> Bool {
        if almostEqual(lhs.x, rhs.x) {
            return lhs.y < rhs.y
        }
        return lhs.x < rhs.y
    }
    
}



