import CGodot

class X {
	var x: Variant? = nil
	var y: UndoRedo? = nil
	
	func demo () -> UndoRedo? {
		return nil
	}

}

extension String {
	var debugDesciption: Swift.String {
		return Swift.String (validatingUTF8: self.utf8().get_data()) ?? "invalid-string"
	}
}
public func dump (x: Node) {
	func nestedDump (x: Node, prefix: Swift.String) {
		let nodeCount = x.get_child_count(true)
		for i in 0..<nodeCount {
			let node = x.get_child(i, true)
			if let node {
				let sn = node.get_name().as_std_string()
				let desc = node.get_description_std_string()
				print ("\(prefix)\(sn) - \(desc)")
				nestedDump(x: node, prefix: prefix + "  ")
			}
		}
	}
	nestedDump(x: x, prefix: "")
}
