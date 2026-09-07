'use client';

import { useState, useEffect, useRef, useCallback } from 'react';
import { Chess } from 'chess.js';
import { Chessboard } from 'react-chessboard';

const PIECE_VALUES = {
  p: 100,
  n: 320,
  b: 330,
  r: 500,
  q: 900,
  k: 20_000,
};

const CHECKMATE_SCORE = 1_000_000;
const SEARCH_TIME_LIMIT_MS = 1_200;

function getPositionalBonus(piece, row, column) {
  const rank = piece.color === 'w' ? 7 - row : row;
  const centerDistance = Math.abs(3.5 - column) + Math.abs(3.5 - rank);
  const centerBonus = Math.max(0, 4 - centerDistance) * 5;

  switch (piece.type) {
    case 'p':
      return rank * 10 + centerBonus;
    case 'n':
    case 'b':
      return centerBonus * 2;
    case 'r':
      return rank * 2;
    case 'q':
      return centerBonus;
    case 'k':
      return rank < 2 ? 18 : -centerBonus;
    default:
      return 0;
  }
}

function evaluatePosition(position) {
  if (position.isCheckmate()) {
    return position.turn() === 'w' ? -CHECKMATE_SCORE : CHECKMATE_SCORE;
  }

  if (position.isDraw()) return 0;

  let score = 0;
  position.board().forEach((rank, row) => {
    rank.forEach((piece, column) => {
      if (!piece) return;

      const pieceScore = PIECE_VALUES[piece.type] + getPositionalBonus(piece, row, column);
      score += piece.color === 'w' ? pieceScore : -pieceScore;
    });
  });

  if (position.inCheck()) {
    score += position.turn() === 'w' ? -35 : 35;
  }

  return score;
}

function scoreMove(move) {
  let score = 0;

  if (move.captured) score += 10 * PIECE_VALUES[move.captured] - PIECE_VALUES[move.piece];
  if (move.promotion) score += PIECE_VALUES[move.promotion] - PIECE_VALUES.p;
  if (move.san.includes('+')) score += 40;
  if (move.san.includes('#')) score += CHECKMATE_SCORE;

  return score;
}

function orderedMoves(position) {
  return position.moves({ verbose: true }).sort((a, b) => scoreMove(b) - scoreMove(a));
}

function minimax(position, depth, alpha, beta, searchState) {
  if (Date.now() >= searchState.deadline) {
    searchState.timedOut = true;
    return evaluatePosition(position);
  }

  if (depth === 0 || position.isGameOver()) {
    return evaluatePosition(position);
  }

  const isWhiteTurn = position.turn() === 'w';
  let bestScore = isWhiteTurn ? -Infinity : Infinity;

  for (const move of orderedMoves(position)) {
    position.move(move);
    const score = minimax(position, depth - 1, alpha, beta, searchState);
    position.undo();

    if (searchState.timedOut) return bestScore;

    if (isWhiteTurn) {
      bestScore = Math.max(bestScore, score);
      alpha = Math.max(alpha, score);
    } else {
      bestScore = Math.min(bestScore, score);
      beta = Math.min(beta, score);
    }

    if (beta <= alpha) break;
  }

  return bestScore;
}

function findBestComputerMove(currentGame) {
  const position = new Chess(currentGame.fen());
  const legalMoves = orderedMoves(position);
  if (legalMoves.length === 0) return null;

  let bestMove = legalMoves[0];
  const maxDepth = legalMoves.length <= 14 ? 4 : 3;
  const deadline = Date.now() + SEARCH_TIME_LIMIT_MS;

  for (let depth = 1; depth <= maxDepth; depth += 1) {
    const searchState = { deadline, timedOut: false };
    let bestScore = Infinity;
    let depthBestMove = bestMove;

    for (const move of legalMoves) {
      position.move(move);
      const score = minimax(position, depth - 1, -Infinity, Infinity, searchState);
      position.undo();

      if (searchState.timedOut) break;

      if (score < bestScore) {
        bestScore = score;
        depthBestMove = move;
      }
    }

    if (searchState.timedOut) break;
    bestMove = depthBestMove;
  }

  return bestMove;
}

export default function ChessGame() {
  const [game, setGame] = useState(new Chess());
  const [engineReady, setEngineReady] = useState(false);
  const [isThinking, setIsThinking] = useState(false);
  const [moveHistory, setMoveHistory] = useState([]);
  const [gameStatus, setGameStatus] = useState("White to move");
  const [orientation, setOrientation] = useState("white");

  // Selection & Dot states
  const [selectedSquare, setSelectedSquare] = useState(null);
  const [optionSquares, setOptionSquares] = useState({});

  const moveEndRef = useRef(null);
  const aiMoveTimeoutRef = useRef(null);
  const isThinkingRef = useRef(false);

  // Wasm Engine Initialization (C++ engine compiled to engine.js + engine.wasm)
  useEffect(() => {
    let cancelled = false;

    const isEngineCallable = () =>
      typeof window !== 'undefined' &&
      window.Module &&
      typeof window.Module.ccall === 'function';

    const markReady = () => {
      if (!cancelled) {
        setEngineReady(true);
        setGameStatus((status) => (status === "Loading Engine..." ? "White to move" : status));
      }
    };

    if (isEngineCallable()) {
      markReady();
      return;
    }

    const previousModule = window.Module || {};
    window.Module = {
      ...previousModule,
      locateFile: (path) => `/${path}`,
      onRuntimeInitialized: () => {
        if (typeof previousModule.onRuntimeInitialized === 'function') {
          previousModule.onRuntimeInitialized();
        }
        markReady();
      },
    };

    if (!document.querySelector('script[src="/engine.js"]')) {
      const script = document.createElement('script');
      script.src = "/engine.js";
      script.async = true;
      script.onerror = () => {
        console.error("Failed to load /engine.js — the built-in legal-move search remains available.");
      };
      document.body.appendChild(script);
    }

    const poll = setInterval(() => {
      if (isEngineCallable()) {
        markReady();
        clearInterval(poll);
      }
    }, 150);

    return () => {
      cancelled = true;
      clearInterval(poll);
    };
  }, []);

  // Auto-scroll move history
  useEffect(() => {
    moveEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [moveHistory]);

  const updateGameStatus = useCallback((currentGame) => {
    if (currentGame.isCheckmate()) {
      setGameStatus(`Checkmate! ${currentGame.turn() === 'w' ? 'Black' : 'White'} wins!`);
    } else if (currentGame.isDraw()) {
      setGameStatus("Draw by stalemate / repetition!");
    } else if (currentGame.inCheck()) {
      setGameStatus(`${currentGame.turn() === 'w' ? 'White' : 'Black'} is in check!`);
    } else {
      setGameStatus(`${currentGame.turn() === 'w' ? 'White' : 'Black'} to move`);
    }
  }, []);

  const updateMoveHistory = useCallback((sanMove) => {
    setMoveHistory((prev) => {
      const lastMove = prev[prev.length - 1];

      if (!lastMove || lastMove.black) {
        return [...prev, { white: sanMove, black: null }];
      }

      return [...prev.slice(0, -1), { ...lastMove, black: sanMove }];
    });
  }, []);

  // Draw Premium Black Dots for valid moves
  const showMoveOptions = useCallback((square, currentGame) => {
    const validMoves = currentGame.moves({ square, verbose: true });
    
    if (validMoves.length === 0) {
      setOptionSquares({});
      return;
    }

    const highlightStyles = {};

    // Highlight selected piece
    highlightStyles[square] = {
      backgroundColor: 'rgba(255, 255, 0, 0.45)',
    };

    validMoves.forEach((move) => {
      const isCapture = currentGame.get(move.to);

      if (isCapture) {
        highlightStyles[move.to] = {
          backgroundImage: 'radial-gradient(circle, transparent 58%, rgba(0, 0, 0, 0.3) 59%, rgba(0, 0, 0, 0.3) 72%, transparent 73%)',
          cursor: 'pointer',
        };
      } else {
        highlightStyles[move.to] = {
          backgroundImage: 'radial-gradient(circle, rgba(0, 0, 0, 0.3) 22%, transparent 24%)',
          cursor: 'pointer',
        };
      }
    });

    setOptionSquares(highlightStyles);
  }, []);

  // Background AI Execution
  const requestAIMove = useCallback((currentGame) => {
    if (currentGame.isGameOver() || isThinkingRef.current) return;

    isThinkingRef.current = true;
    setIsThinking(true);
    setGameStatus("AI is evaluating tree...");

    aiMoveTimeoutRef.current = window.setTimeout(() => {
      try {
        const selectedMove = findBestComputerMove(currentGame);
        if (!selectedMove) return;

        const gameCopy = new Chess(currentGame.fen());
        const moveResult = gameCopy.move(selectedMove);

        if (moveResult) {
          setGame(gameCopy);
          updateMoveHistory(moveResult.san);
          updateGameStatus(gameCopy);
        }
      } catch (err) {
        console.error("AI execution error:", err);
      } finally {
        aiMoveTimeoutRef.current = null;
        isThinkingRef.current = false;
        setIsThinking(false);
      }
    }, 50);
  }, [updateGameStatus, updateMoveHistory]);

  const applyPlayerMove = useCallback((from, to) => {
    if (isThinking || isThinkingRef.current || game.isGameOver() || !from || !to) return false;

    const gameCopy = new Chess(game.fen());
    try {
      const moveResult = gameCopy.move({
        from,
        to,
        promotion: 'q',
      });

      if (!moveResult) return false;

      setGame(gameCopy);
      updateMoveHistory(moveResult.san);
      updateGameStatus(gameCopy);
      setSelectedSquare(null);
      setOptionSquares({});
      requestAIMove(gameCopy);
      return true;
    } catch {
      return false;
    }
  }, [game, isThinking, requestAIMove, updateGameStatus, updateMoveHistory]);

  const handleSquareClick = useCallback(({ square }) => {
    if (isThinking || game.isGameOver() || !square) return;

    const clickedPiece = game.get(square);

    if (!selectedSquare) {
      if (clickedPiece && clickedPiece.color === game.turn()) {
        setSelectedSquare(square);
        showMoveOptions(square, game);
      }
      return;
    }

    if (selectedSquare === square) {
      setSelectedSquare(null);
      setOptionSquares({});
      return;
    }

    if (clickedPiece && clickedPiece.color === game.turn()) {
      setSelectedSquare(square);
      showMoveOptions(square, game);
      return;
    }

    if (!applyPlayerMove(selectedSquare, square)) {
      setSelectedSquare(null);
      setOptionSquares({});
    }
  }, [applyPlayerMove, game, isThinking, selectedSquare, showMoveOptions]);

  const handlePieceDrop = useCallback(({ sourceSquare, targetSquare }) => {
    if (!targetSquare) return false;
    return applyPlayerMove(sourceSquare, targetSquare);
  }, [applyPlayerMove]);

  // FIXED: Button Handlers
  const toggleFlip = useCallback(() => {
    setOrientation((prev) => (prev === "white" ? "black" : "white"));
  }, []);

  const handleReset = useCallback(() => {
    if (aiMoveTimeoutRef.current !== null) {
      window.clearTimeout(aiMoveTimeoutRef.current);
      aiMoveTimeoutRef.current = null;
    }

    isThinkingRef.current = false;
    setGame(new Chess());
    setMoveHistory([]);
    setGameStatus("White to move");
    setSelectedSquare(null);
    setOptionSquares({});
    setIsThinking(false);
    setOrientation("white"); // Also resets the board angle
  }, []);

  return (
    <div className="min-h-screen bg-[#161512] text-[#c3c2c1] flex flex-col justify-between font-sans select-none">
      
      <header className="h-14 border-b border-[#2b2925] bg-[#21201d] px-6 flex items-center justify-between">
        <div className="flex items-center space-x-3">
          <div className="w-7 h-7 bg-[#81b64c] rounded flex items-center justify-center font-bold text-black text-lg shadow">
            ♟
          </div>
          <span className="font-extrabold tracking-wider text-white text-lg">ALPHA<span className="text-[#81b64c]">CHESS</span></span>
          <span className="text-xs px-2 py-0.5 rounded bg-[#2b2925] text-zinc-400 border border-[#3b3834]">Wasm C++ Core</span>
        </div>

        <div className="flex items-center space-x-3">
          <span className={`inline-block w-2.5 h-2.5 rounded-full ${engineReady ? 'bg-[#81b64c]' : 'bg-amber-500 animate-pulse'}`} />
          <span className="text-xs font-semibold text-zinc-300">
            {engineReady ? "Engine Ready" : "Loading Engine..."}
          </span>
        </div>
      </header>

      <main className="flex-1 flex flex-col lg:flex-row items-center justify-center gap-8 p-4 lg:p-8 max-w-7xl mx-auto w-full">
        
        <div className="flex flex-col items-center w-full max-w-[560px]">
          
          <div className="w-full flex items-center justify-between bg-[#21201d] px-4 py-2.5 rounded-t-md border-t border-x border-[#2b2925]">
            <div className="flex items-center space-x-3">
              <div className="w-9 h-9 rounded bg-[#2b2925] border border-[#3b3834] flex items-center justify-center text-xl">
                🤖
              </div>
              <div>
                <div className="flex items-center space-x-2">
                  <span className="font-bold text-white text-sm">AlphaEngine Wasm</span>
                  <span className="text-[11px] bg-[#3b3834] text-zinc-300 px-1.5 rounded font-mono">1850</span>
                </div>
                <div className="text-[11px] text-zinc-400">Legal alpha-beta search • 3–4 ply</div>
              </div>
            </div>
            {isThinking && (
              <div className="flex items-center space-x-1.5 text-xs text-[#81b64c] bg-[#81b64c]/10 px-2.5 py-1 rounded border border-[#81b64c]/30">
                <span className="w-1.5 h-1.5 rounded-full bg-[#81b64c] animate-ping" />
                <span className="font-semibold font-mono">Thinking...</span>
              </div>
            )}
          </div>

          <div className="w-full shadow-2xl border-x border-[#2b2925] bg-[#2b2925]">
            <Chessboard
              options={{
                position: game.fen(),
                allowDragging: !isThinking && !game.isGameOver(),
                onPieceDrop: handlePieceDrop,
                onSquareClick: handleSquareClick,
                squareStyles: optionSquares,
                boardOrientation: orientation,
                darkSquareStyle: { backgroundColor: '#739552' },
                lightSquareStyle: { backgroundColor: '#ebecd0' },
                animationDurationInMs: 150,
                canDragPiece: ({ square }) => {
                  if (isThinking || game.isGameOver() || !square) return false;
                  const piece = game.get(square);
                  return Boolean(piece && piece.color === game.turn());
                },
              }}
            />
          </div>

          <div className="w-full flex items-center justify-between bg-[#21201d] px-4 py-2.5 rounded-b-md border-b border-x border-[#2b2925]">
            <div className="flex items-center space-x-3">
              <div className="w-9 h-9 rounded bg-[#81b64c]/20 border border-[#81b64c]/30 flex items-center justify-center text-xl text-[#81b64c]">
                👤
              </div>
              <div>
                <div className="flex items-center space-x-2">
                  <span className="font-bold text-white text-sm">You</span>
                  <span className="text-[11px] bg-[#3b3834] text-zinc-300 px-1.5 rounded font-mono">1500</span>
                </div>
                <div className="text-[11px] text-zinc-400">Guest Player</div>
              </div>
            </div>
            <div className="text-xs font-mono text-zinc-400">
              {game.turn() === 'w' ? 'White to move' : 'Black to move'}
            </div>
          </div>
        </div>

        <div className="w-full lg:w-80 h-[560px] bg-[#21201d] border border-[#2b2925] rounded-md flex flex-col justify-between shadow-xl">
          <div className="px-4 py-3 border-b border-[#2b2925] flex items-center justify-between bg-[#262421]">
            <span className="text-sm font-semibold text-white">{gameStatus}</span>
            <span className="text-xs font-mono text-zinc-500">M{Math.floor((moveHistory.length * 2) / 2) + 1}</span>
          </div>

          <div className="flex-1 overflow-y-auto px-4 py-2 font-mono text-sm space-y-1 select-none">
            {moveHistory.length === 0 ? (
              <div className="h-full flex items-center justify-center text-zinc-600 text-xs italic">
                Click any piece to view move options
              </div>
            ) : (
              moveHistory.map((entry, index) => (
                <div key={index} className={`flex items-center py-1 px-2 rounded ${index % 2 === 0 ? 'bg-[#262421]' : 'bg-transparent'}`}>
                  <span className="w-10 text-zinc-500 text-xs font-semibold">{index + 1}.</span>
                  <span className="w-24 font-bold text-zinc-200">{entry.white}</span>
                  <span className="w-24 font-bold text-zinc-300">{entry.black || ""}</span>
                </div>
              ))
            )}
            <div ref={moveEndRef} />
          </div>

          <div className="bg-[#1b1a18] p-3 border-t border-[#2b2925] text-xs font-mono text-zinc-400 space-y-1">
            <div className="flex justify-between">
              <span>Mode:</span>
              <span className="text-[#81b64c] font-bold">Pro Click-to-Move</span>
            </div>
            <div className="flex justify-between">
              <span>Search:</span>
              <span className="text-zinc-200">Alpha-beta • 3–4 ply</span>
            </div>
            <div className="flex justify-between">
              <span>Engine Status:</span>
              <span className="text-zinc-200">{engineReady ? 'Wasm + legal search' : 'Legal search active'}</span>
            </div>
          </div>

          {/* FIXED BUTTONS */}
          <div className="p-3 border-t border-[#2b2925] grid grid-cols-2 gap-2 bg-[#262421]">
            <button
              type="button"
              onClick={toggleFlip}
              className="px-3 py-2 bg-[#2b2925] hover:bg-[#363430] active:scale-95 text-zinc-300 font-semibold text-xs rounded transition-all border border-[#3b3834]"
            >
              Flip Board
            </button>
            <button
              type="button"
              onClick={handleReset}
              className="px-3 py-2 bg-[#81b64c] hover:bg-[#72a343] active:scale-95 text-white font-bold text-xs rounded transition-all shadow"
            >
              New Game
            </button>
          </div>
        </div>

      </main>

      <footer className="h-8 border-t border-[#2b2925] bg-[#1a1917] px-6 flex items-center justify-between text-[11px] text-zinc-500">
        <span>Highly Efficient Architecture for Low-End Devices</span>
        <span>Next.js • Wasm • C++</span>
      </footer>

    </div>
  );
}
