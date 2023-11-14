import CGodot

class X {
	var x: Variant? = nil
	var y: UndoRedo? = nil
	
	func demo () -> UndoRedo? {
		return nil
	}

}

public func dump (x: Node) {
	let nodeCount = x.get_child_count(true)
	for i in 0..<nodeCount {
		var node = x.get_child(i, true)
	}
}
