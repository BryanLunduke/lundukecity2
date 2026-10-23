import SwiftUI
import SpriteKit

/// Minimal SpriteKit "tap the falling blocks" game used to exercise xtool.
final class GameScene: SKScene {
    private var score = 0
    private let label = SKLabelNode(fontNamed: "Menlo-Bold")
    private var lastSpawn: TimeInterval = 0

    override func didMove(to view: SKView) {
        backgroundColor = .black
        label.fontSize = 28
        label.position = CGPoint(x: size.width / 2, y: size.height - 80)
        label.text = "Score: 0"
        addChild(label)
    }

    override func update(_ currentTime: TimeInterval) {
        if currentTime - lastSpawn > 0.8 {
            lastSpawn = currentTime
            let block = SKShapeNode(rectOf: CGSize(width: 60, height: 60), cornerRadius: 8)
            block.fillColor = SKColor(hue: .random(in: 0...1), saturation: 0.8, brightness: 1, alpha: 1)
            block.name = "block"
            block.position = CGPoint(x: .random(in: 40...(size.width - 40)), y: size.height + 40)
            addChild(block)
            block.run(.sequence([.moveTo(y: -40, duration: 3), .removeFromParent()]))
        }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        for node in nodes(at: touch.location(in: self)) where node.name == "block" {
            node.removeAllActions()
            node.run(.sequence([.scale(to: 0, duration: 0.15), .removeFromParent()]))
            score += 1
            label.text = "Score: \(score)"
        }
    }
}

struct ContentView: View {
    var scene: SKScene {
        let s = GameScene(size: CGSize(width: 390, height: 844))
        s.scaleMode = .resizeFill
        return s
    }

    var body: some View {
        SpriteView(scene: scene)
            .ignoresSafeArea()
    }
}
